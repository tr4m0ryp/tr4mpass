#!/usr/bin/env bash
#
# fetch_images.sh -- Path A phase 2: pull the images path_a_ramdisk.c
# needs out of the IPSW of the INSTALLED iOS version, via HTTP Range
# requests only (no multi-GB download).
#
# The script is device-agnostic:
#   - the ProductType and board come from detect_ios.sh (ios_model.txt /
#     ios_board.txt) or from --model / --board;
#   - every entry is resolved from BuildManifest.plist, not from a
#     hardcoded board name. Older and newer IPSW layouts are handled the
#     same way: iBSS, iBEC, DeviceTree, RestoreRamDisk and the optional
#     RestoreTrustCache are read from the matching BuildIdentity.
#   - if the manifest contains several board variants for the same
#     ProductType (for example a dual-sourced SoC), all variants are
#     fetched and the first is exported as the primary; the others are
#     exported as *_ALT.
#
# WHAT IS NOT DONE here (Phase 3 territory):
#   - no img4/IM4P decoding: the iBSS/iBEC .im4p files are raw IM4P
#     payloads, not bootable IMG4 containers;
#   - no decryption: the restore ramdisk / OS dmg may be AES-encrypted;
#     a custom ramdisk needs the ipsw(1) pipeline or equivalent (plan
#     Phase 3);
#   - no personalized-IMG4 rebuild / ApNonce handling.
#   The iBSS smoke test works on the raw IM4P because pwned DFU's stage
#   loader accepts the payload sent by irecovery as-is.
#
# Usage: fetch_images.sh [--model <ProductType>] [--board <BoardConfig>]
#                        [--build BUILDID] [--out-dir DIR]
# Writes files into the out dir (default "$(state_dir)/images", env
# override TR4MPASS_IMAGES_DIR) plus env.sh there, and prints sha256 +
# size for every artifact. Never touches /tmp.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/lib.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "[fetch-images] $*"; }

DEVICE_MODEL=""
DEVICE_BOARD=""
BUILD=""
OUT_DIR_OVERRIDE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --model)   [ $# -ge 2 ] || fail "--model needs a value"
                   DEVICE_MODEL="$2";       shift 2 ;;
        --device)  [ $# -ge 2 ] || fail "--device needs a value"
                   DEVICE_MODEL="$2";       shift 2 ;;
        --board)   [ $# -ge 2 ] || fail "--board needs a value"
                   DEVICE_BOARD="$2";       shift 2 ;;
        --build)   [ $# -ge 2 ] || fail "--build needs a value"
                   BUILD="$2";              shift 2 ;;
        --out-dir) [ $# -ge 2 ] || fail "--out-dir needs a value"
                   OUT_DIR_OVERRIDE="$2";   shift 2 ;;
        -h|--help) sed -n '2,60p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

command -v python3 >/dev/null 2>&1 || fail "python3 not found"
command -v shasum  >/dev/null 2>&1 || fail "shasum not found"

STATE="$(state_dir)"

if [ -z "$DEVICE_MODEL" ]; then
    MODEL_FILE="$STATE/ios_model.txt"
    [ -s "$MODEL_FILE" ] || fail "no model given and $MODEL_FILE is missing/empty -- run scripts/detect_ios.sh first (or pass --model <ProductType>)"
    DEVICE_MODEL="$(cat "$MODEL_FILE")"
fi

if [ -z "$DEVICE_BOARD" ]; then
    BOARD_FILE="$STATE/ios_board.txt"
    [ -s "$BOARD_FILE" ] && DEVICE_BOARD="$(cat "$BOARD_FILE")"
fi

if [ -z "$BUILD" ]; then
    BUILD_FILE="$STATE/ios_build.txt"
    [ -s "$BUILD_FILE" ] || fail "no build given and $BUILD_FILE is missing/empty -- run scripts/detect_ios.sh first (or pass --build BUILDID)"
    BUILD="$(cat "$BUILD_FILE")"
    info "using detected build from $(basename "$BUILD_FILE"): $BUILD"
fi

info "device model: $DEVICE_MODEL${DEVICE_BOARD:+ (board $DEVICE_BOARD)}"

# Images live under the state dir layout (never /tmp).
OUT_DIR="${OUT_DIR_OVERRIDE:-${TR4MPASS_IMAGES_DIR:-$(state_dir)/images}}"
mkdir -p "$OUT_DIR" || fail "cannot create output dir: $OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

IPSW_URL="$(python3 "$SCRIPT_DIR/ipsw_pull.py" url-for "$DEVICE_MODEL" "$BUILD")" \
    || fail "could not resolve IPSW URL for $DEVICE_MODEL build $BUILD"
info "IPSW: $IPSW_URL"

# ------------------------------------------------------------------
# Resolve every needed entry from BuildManifest.plist.
# Prints lines: IBSS=<path>, IBEC=<path>, DEVICETREE=<path>,
#               RAMDISK=<path>, TRUSTCACHE=<path> (optional)
# ------------------------------------------------------------------
MANIFEST_OUT="$(python3 - "$IPSW_URL" "$SCRIPT_DIR" "$DEVICE_MODEL" "$DEVICE_BOARD" <<'PYEOF'
import sys, struct, zlib, plistlib, re, os

url, script_dir, model, board = sys.argv[1:5]
sys.path.insert(0, script_dir)
import ipsw_pull

def cd_fixed():
    """[(name, comp_size, local_offset)] with correct zip64 fields."""
    total = ipsw_pull.content_length(url)
    tail_len = min(total, 66560)
    tail = ipsw_pull.get(url, total - tail_len, total - 1)
    pos = tail.rfind(ipsw_pull.EOCD_SIG)
    if pos < 0:
        raise RuntimeError("EOCD not found")
    (_, _, _, _, n, cd_size, cd_off, _) = struct.unpack("<4sHHHHIIH", tail[pos:pos + 22])
    if cd_off == 0xFFFFFFFF or n == 0xFFFF:
        loc = tail.rfind(ipsw_pull.EOCD64_LOC_SIG, 0, pos)
        o = struct.unpack("<Q", tail[loc + 8:loc + 16])[0]
        rec = ipsw_pull.get(url, o, o + 55)
        (_, _, _, _, _, _, _, n, cd_size, cd_off) = struct.unpack(
            "<4sQHHIIQQQQ", rec[:56])
    cd = ipsw_pull.get(url, cd_off, cd_off + cd_size - 1)
    out, i = [], 0
    while i + 46 <= len(cd) and cd[i:i + 4] == b"PK\x01\x02":
        csize = struct.unpack("<I", cd[i + 20:i + 24])[0]
        usize = struct.unpack("<I", cd[i + 24:i + 28])[0]
        nlen, elen, clen = struct.unpack("<HHH", cd[i + 28:i + 34])
        loff = struct.unpack("<I", cd[i + 42:i + 46])[0]
        name = cd[i + 46:i + 46 + nlen].decode("utf-8", "replace")
        ex = cd[i + 46 + nlen:i + 46 + nlen + elen]
        j = 0
        while j + 4 <= len(ex):
            hid, hsz = struct.unpack("<HH", ex[j:j + 4])
            if hid == 1:
                k = j + 4
                if usize == 0xFFFFFFFF:
                    usize = struct.unpack("<Q", ex[k:k + 8])[0]; k += 8
                if csize == 0xFFFFFFFF:
                    csize = struct.unpack("<Q", ex[k:k + 8])[0]; k += 8
                if loff == 0xFFFFFFFF:
                    loff = struct.unpack("<Q", ex[k:k + 8])[0]; k += 8
            j += 4 + hsz
        out.append((name, csize, loff))
        i += 46 + nlen + elen + clen
    return out

def entry_blob(name, comp, loff):
    lh = ipsw_pull.get(url, loff, loff + 29)
    if lh[:4] != b"PK\x03\x04":
        raise RuntimeError("bad local header for %s" % name)
    nl, el = struct.unpack("<HH", lh[26:30])
    blob = ipsw_pull.get(url, loff + 30 + nl + el,
                         loff + 30 + nl + el + comp - 1)
    if struct.unpack("<H", lh[8:10])[0] == 8:
        blob = zlib.decompress(blob, -15)
    return blob

entries = cd_fixed()
bm = next(e for e in entries if e[0] == "BuildManifest.plist")
pl = plistlib.loads(entry_blob(bm[0], bm[1], bm[2]))

identities = pl.get("BuildIdentities", [])

def matches(ident):
    info = ident.get("Info", {})
    if info.get("ProductType") != model:
        return False
    if board and info.get("DeviceClass") != board:
        return False
    return True

candidates = [i for i in identities if matches(i)]
if not candidates:
    # Some manifests omit ProductType; fall back to DeviceClass when the
    # caller supplied a board explicitly.
    if board:
        candidates = [i for i in identities
                      if i.get("Info", {}).get("DeviceClass") == board]
if not candidates:
    sys.exit("no BuildIdentity found for ProductType=%s board=%s" % (model, board or "*"))

# Prefer the Erase restore identity, then Keep the first one.
erase = [i for i in candidates
         if i.get("Info", {}).get("RestoreBehavior") == "Erase"]
selected = erase or candidates

def entry_path(ident, key):
    return ident.get("Manifest", {}).get(key, {}).get("Info", {}).get("Path")

seen = set()
def emit(key, path):
    if not path:
        return
    line = "%s=%s" % (key, path)
    if line not in seen:
        seen.add(line)
        print(line)

for ident in selected:
    for key in ("iBSS", "iBEC", "DeviceTree", "RestoreRamDisk",
                "RestoreTrustCache", "TrustCache"):
        emit(key.upper(), entry_path(ident, key))

# If the manifest has no explicit trustcache entry, look for the matching
# <ramdisk>.trustcache in the central directory.
ramdisks = [entry_path(i, "RestoreRamDisk") for i in selected]
ramdisks = [r for r in ramdisks if r]
if ramdisks:
    base = os.path.basename(ramdisks[0])
    for name, _, _ in entries:
        if name.endswith(base + ".trustcache") or re.search(
                r"(^|/)" + re.escape(base) + r"\.trustcache$", name):
            emit("TRUSTCACHE", name)
            break
PYEOF
)" || fail "BuildManifest parse failed"

IBSS_PATHS=(); IBEC_PATHS=(); DT_PATHS=(); RD_PATHS=(); TC_PATHS=()
while IFS='=' read -r k v; do
    [ -n "${k:-}" ] || continue
    case "$k" in
        IBSS)        IBSS_PATHS+=("$v") ;;
        IBEC)        IBEC_PATHS+=("$v") ;;
        DEVICETREE)  DT_PATHS+=("$v") ;;
        RAMDISK)     RD_PATHS+=("$v") ;;
        TRUSTCACHE)  TC_PATHS+=("$v") ;;
    esac
done <<< "$MANIFEST_OUT"

[ "${#IBSS_PATHS[@]}" -gt 0 ] || fail "BuildManifest has no iBSS entry for $DEVICE_MODEL"
[ "${#IBEC_PATHS[@]}" -gt 0 ] || fail "BuildManifest has no iBEC entry for $DEVICE_MODEL"
[ "${#DT_PATHS[@]}"   -gt 0 ] || fail "BuildManifest has no DeviceTree entry for $DEVICE_MODEL"
[ "${#RD_PATHS[@]}"   -gt 0 ] || fail "BuildManifest has no RestoreRamDisk entry for $DEVICE_MODEL"

# ------------------------------------------------------------------
# Fetch exact entries and return the written basename.
# ------------------------------------------------------------------
fetch_path() {
    local path="$1" base regex
    base="$(basename "$path")"
    regex="$(python3 -c 'import re,sys; print(re.escape(sys.argv[1]))' "$path")"
    info "fetching $base ..." >&2
    python3 "$SCRIPT_DIR/ipsw_pull.py" fetch "$IPSW_URL" "$regex" "$OUT_DIR" >/dev/null 2>&1 \
        || fail "fetch failed for $path"
    require_file "$base" "$OUT_DIR/$base"
    printf '%s\n' "$base"
}

IBSS_BASES=(); IBEC_BASES=(); DT_BASES=(); RD_BASES=(); TC_BASES=()
for p in "${IBSS_PATHS[@]}";       do IBSS_BASES+=("$(fetch_path "$p")"); done
for p in "${IBEC_PATHS[@]}";       do IBEC_BASES+=("$(fetch_path "$p")"); done
for p in "${DT_PATHS[@]}";         do DT_BASES+=("$(fetch_path "$p")"); done
for p in "${RD_PATHS[@]}";         do RD_BASES+=("$(fetch_path "$p")"); done
for p in "${TC_PATHS[@]}";         do TC_BASES+=("$(fetch_path "$p")"); done

# Deduplicate basenames while keeping order.
dedupe() {
    printf '%s\n' "$@" | awk 'NF && !seen[$0]++'
}
IBSS_BASES=($(dedupe "${IBSS_BASES[@]}"))
IBEC_BASES=($(dedupe "${IBEC_BASES[@]}"))
DT_BASES=($(dedupe "${DT_BASES[@]}"))
RD_BASES=($(dedupe "${RD_BASES[@]}"))
TC_BASES=($(dedupe "${TC_BASES[@]}") )

# ------------------------------------------------------------------
# env.sh: absolute paths, sourced by run_path_a.sh / later phases.
# ------------------------------------------------------------------
ENV_SH="$OUT_DIR/env.sh"
{
    echo "# Generated by scripts/fetch_images.sh (IPSW $DEVICE_MODEL${DEVICE_BOARD:+ / $DEVICE_BOARD} build $BUILD)."
    echo "# Entries were resolved from BuildManifest.plist -- no board name is assumed."
    echo "# NOTE: the .im4p files are raw IM4P payloads, NOT decoded/decrypted"
    echo "# IMG4s -- conversion (and ramdisk patching) happens in Phase 3."
    echo "export TR4MPASS_IBSS_PATH=\"$OUT_DIR/${IBSS_BASES[0]}\""
    if [ "${#IBSS_BASES[@]}" -gt 1 ]; then
        echo "export TR4MPASS_IBSS_PATH_ALT=\"$OUT_DIR/${IBSS_BASES[1]}\""
    fi
    echo "export TR4MPASS_IBEC_PATH=\"$OUT_DIR/${IBEC_BASES[0]}\""
    if [ "${#IBEC_BASES[@]}" -gt 1 ]; then
        echo "export TR4MPASS_IBEC_PATH_ALT=\"$OUT_DIR/${IBEC_BASES[1]}\""
    fi
    echo "export TR4MPASS_DEVICETREE_PATH=\"$OUT_DIR/${DT_BASES[0]}\""
    echo "export TR4MPASS_RAMDISK_PATH=\"$OUT_DIR/${RD_BASES[0]}\""
    if [ "${#TC_BASES[@]}" -gt 0 ]; then
        echo "export TR4MPASS_TRUSTCACHE_PATH=\"$OUT_DIR/${TC_BASES[0]}\""
    fi
} > "$ENV_SH"

# ------------------------------------------------------------------
# Summary: sha256 + size of every artifact, validated via require_file.
# ------------------------------------------------------------------
info "artifacts:"
for base in "${IBSS_BASES[@]}" "${IBEC_BASES[@]}" "${DT_BASES[@]}" "${RD_BASES[@]}" "${TC_BASES[@]}"; do
    [ -n "$base" ] || continue
    f="$OUT_DIR/$base"
    require_file "$base" "$f"
    SIZE=$(wc -c < "$f" | tr -d ' ')
    SHA=$(shasum -a 256 "$f" | awk '{print $1}')
    printf '  %-40s %10s bytes  sha256=%s\n' "$base" "$SIZE" "$SHA"
done

info "env file: $ENV_SH  (source it to export TR4MPASS_*_PATH)"
