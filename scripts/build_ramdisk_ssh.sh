#!/usr/bin/env bash
#
# build_ramdisk_ssh.sh -- Path A phase 3, option A: build an SSH ramdisk
# for the target device from the fetched IPSW images, using existing
# tools only (user directive: WRAP, do not reimplement).
#
# VERIFICATION LOG (every factual claim below was checked, dated 2026-11-01;
# `ipsw` was NOT installed on this machine at verification time, so all
# ipsw facts come from the upstream repo/docs, not local --help output):
#
#   1. `ipsw ssh` EXISTS but does NOT build ramdisks. Source:
#      https://github.com/blacktop/ipsw/blob/master/cmd/ipsw/cmd/ssh/ssh.go
#      -- Use: "ssh", Short: "SSH into a jailbroken device" (client wrapper:
#      --host/--port/--user/--password/--key/--insecure). Its only
#      subcommands are `ssh debugserver` and `ssh shsh`.
#      Docs: https://blacktop.github.io/ipsw/docs/cli/ipsw/ssh/ssh
#   2. No SSH-ramdisk builder subcommand exists in ipsw master (tree walk of
#      cmd/ipsw/cmd/**, 2026-11-01): the only "ramdisk" command file is
#      cmd/ipsw/cmd/idev/idev_restore_ramdisk.go, a HIDDEN stub whose RunE is
#      literally `panic("not implemented")`.
#      https://github.com/blacktop/ipsw/blob/master/cmd/ipsw/cmd/idev/idev_restore_ramdisk.go
#   3. History check: no commit in cmd/ipsw/cmd/ssh/ ever added or removed a
#      `ramdisk` subcommand, and no www/docs/cli/ipsw/ssh/ramdisk.md doc page
#      ever existed (GitHub commits?path= queries returned empty).
#      docs/execution-plan.md §3.0 row 2 ("ipsw ssh ... build SSH ramdisk -- to be confirmed")
#      is therefore answered: ipsw CANNOT currently build an SSH ramdisk.
#   4. `ipsw download ipsw` IS real and its flags are verified:
#      https://github.com/blacktop/ipsw/blob/master/www/docs/cli/ipsw/download/ipsw.md
#      e.g. `ipsw download ipsw --device <ProductType> --build <BuildID> --output DIR
#      --confirm` (-b/--build, -d/--device, -o/--output, -y/--confirm,
#      --latest, --urls). Downloads a FULL multi-GB IPSW.
#   5. Install command verified:
#      https://blacktop.github.io/ipsw/docs/getting-started/installation
#      -> `brew install blacktop/tap/ipsw`
#   6. Option B (prebuilt SSH ramdisk from dora2-iOS) is UNVERIFIED: the
#      GitHub account https://github.com/dora2-iOS (id 42214504) now shows
#      public_repos: 0 and name "Inactive"; https://github.com/dora2-iOS/
#      homeenvx/releases returns 404, as does dora2-iOS/jbinit (also cited in
#      PLAN §3.0 -- that link needs re-checking separately). No concrete
#      release URL => option B gets NO code in this script.
#   7. Closest existing ipsw capability (NOT used here, documented for PLAN
#      option C): `ipsw mount rdisk IPSW --ident Erase [--key K | --lookup]`
#      https://blacktop.github.io/ipsw/docs/cli/ipsw/mount -- mounts the
#      (decrypted) RestoreRamDisk on the HOST. Mounting is not patching: it
#      neither injects dropbear/jbinit nor rebuilds a bootable IMG4.
#
# WHAT THIS SCRIPT DOES:
#   - Requires `ipsw` (brew install blacktop/tap/ipsw) and records its
#     version in the log as evidence.
#   - Resolves the IPSW: --ipsw PATH if given, else downloads the full IPSW
#     for --build BUILDID (default: "$(state_dir)/ios_build.txt") via the
#     verified `ipsw download ipsw` subcommand.
#   - Probes the INSTALLED ipsw for a ramdisk-building subcommand
#     (`ipsw ssh ramdisk`). If present in some future/newer release, runs it
#     against the IPSW, collects the produced dmg into --out-dir, and
#     refreshes env.sh there overriding TR4MPASS_RAMDISK_PATH to the NEW ssh
#     ramdisk (this replaces the stock erase ramdisk fetched by
#     fetch_images.sh, which is only patching base material).
#   - If the probe fails (expected on every current release, see finding 2/3)
#     it FAILS with the exact upgrade command -- no workaround, per plan
#     directive -- and points at PLAN §3.0 options B (unverified) and C.
#
# WHAT THIS SCRIPT DOES NOT DO (honesty block):
#   - No decryption of the Firmware/dfu/*.im4p images: none is NEEDED for the
#     pwned-DFU iBSS/iBEC smoke test (the stage loader takes the raw IM4P
#     payload as-is, see fetch_images.sh header), but a BOOTABLE SSH ramdisk
#     needs the encrypted RestoreRamDisk dmg DECRYPTED (AES, per-build WKEY on
#     the A9) plus patched iBEC/iBoot (iBoot64Patcher) and dropbear/jbinit
#     injection -- none of which ipsw exposes as a subcommand today.
#   - No mobileactivationd bundling: that stays an open blocker (PLAN §3.0,
#     TR4MPASS_PATCHED_MAD external input); no verified prebuilt source.
#   - No /tmp writes: everything lives under --out-dir / the state dir
#     (the ipsw binary's own internals are its own business).
#
# Usage: build_ramdisk_ssh.sh [--ipsw PATH] [--build BUILDID]
#                             [--model <ProductType>] [--out-dir DIR]
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib.sh
. "$SCRIPT_DIR/lib.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "[build-ramdisk] $*"; }
usage() { sed -n '2,60p' "$SCRIPT_DIR/build_ramdisk_ssh.sh"; }

DEVICE_MODEL=""
BUILD=""
IPSW_PATH=""
OUT_DIR_OVERRIDE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --ipsw)    [ $# -ge 2 ] || fail "--ipsw needs a value"
                   IPSW_PATH="$2";          shift 2 ;;
        --build)   [ $# -ge 2 ] || fail "--build needs a value"
                   BUILD="$2";              shift 2 ;;
        --model)   [ $# -ge 2 ] || fail "--model needs a value"
                   DEVICE_MODEL="$2";       shift 2 ;;
        --out-dir) [ $# -ge 2 ] || fail "--out-dir needs a value"
                   OUT_DIR_OVERRIDE="$2";   shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$DEVICE_MODEL" ]; then
    MODEL_FILE="$(state_dir)/ios_model.txt"
    [ -s "$MODEL_FILE" ] || fail "no model given and $MODEL_FILE is missing/empty -- run scripts/detect_ios.sh first (or pass --model <ProductType>)"
    DEVICE_MODEL="$(cat "$MODEL_FILE")"
    info "using detected model from $(basename "$MODEL_FILE"): $DEVICE_MODEL"
fi

# ------------------------------------------------------------------
# 0. Preconditions: ipsw present (verified install command), log ready.
# ------------------------------------------------------------------
if ! command -v ipsw >/dev/null 2>&1; then
    fail "ipsw not found -- install it first: brew install blacktop/tap/ipsw"
fi

LOGFILE="$(log_dir)/build-ramdisk-$(date +%Y%m%d-%H%M%S).log"
info "log file: $LOGFILE"

ipsw version 2>&1 | tee -a "$LOGFILE"

# ------------------------------------------------------------------
# 1. Resolve the IPSW: local path, else full download (multi-GB).
# ------------------------------------------------------------------
OUT_DIR="${OUT_DIR_OVERRIDE:-${TR4MPASS_IMAGES_DIR:-$(state_dir)/images}}"
mkdir -p "$OUT_DIR" || fail "cannot create output dir: $OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

if [ -n "$IPSW_PATH" ]; then
    require_file ipsw "$IPSW_PATH"
else
    if [ -z "$BUILD" ]; then
        BUILD_FILE="$(state_dir)/ios_build.txt"
        [ -s "$BUILD_FILE" ] || fail "no build given and $BUILD_FILE is missing/empty -- run scripts/detect_ios.sh first (or pass --build BUILDID)"
        BUILD="$(cat "$BUILD_FILE")"
        info "using detected build from $(basename "$BUILD_FILE"): $BUILD"
    fi
    info "downloading FULL IPSW for $DEVICE_MODEL build $BUILD (multi-GB) ..."
    ipsw download ipsw --device "$DEVICE_MODEL" --build "$BUILD" \
        --output "$OUT_DIR" --confirm 2>&1 | tee -a "$LOGFILE" \
        || fail "ipsw download ipsw failed for build $BUILD"
    IPSW_PATH="$(ls -t "$OUT_DIR"/*.ipsw 2>/dev/null | head -n 1)"
    [ -n "$IPSW_PATH" ] || fail "no .ipsw found in $OUT_DIR after download"
fi
info "IPSW: $IPSW_PATH"

# ------------------------------------------------------------------
# 2. Capability probe (the gate): does THIS ipsw build SSH ramdisks?
#    Verified 2026-11-01: it does not, in any known release (finding 2/3
#    in the header). The probe keeps the script correct for free if a
#    future release adds the subcommand -- we still write no new code.
# ------------------------------------------------------------------
if ipsw ssh ramdisk --help >/dev/null 2>&1; then
    info "probe: 'ipsw ssh ramdisk' exists in this build -- running it"
    ipsw ssh ramdisk --help 2>&1 | tee -a "$LOGFILE"
    START_TS="$(date +%s)"
    ipsw ssh ramdisk "$IPSW_PATH" 2>&1 | tee -a "$LOGFILE" \
        || fail "ipsw ssh ramdisk failed (log: $LOGFILE)"
    # Output location/args of that subcommand are UNVERIFIED (it does not
    # exist in any release we could check); collect the newest .dmg written
    # since the run started, in the CWD or the out dir.
    RAMDISK_SRC="$(find "$PWD" "$OUT_DIR" -maxdepth 1 -name '*.dmg' -newermt "@$START_TS" 2>/dev/null | head -n 1)"
    [ -n "$RAMDISK_SRC" ] || fail "no .dmg produced by ipsw ssh ramdisk (log: $LOGFILE)"
    RAMDISK_OUT="$OUT_DIR/ssh_ramdisk_${BUILD:-local}.dmg"
    cp -f "$RAMDISK_SRC" "$RAMDISK_OUT" || fail "cannot collect ramdisk into $OUT_DIR"
else
    echo "FAIL: this ipsw build has no SSH-ramdisk builder (probe: ipsw ssh ramdisk --help)." >&2
    echo "      Verified 2026-11-01 against ipsw master: 'ipsw ssh' is only an SSH client" >&2
    echo "      wrapper (https://github.com/blacktop/ipsw/tree/master/cmd/ipsw/cmd/ssh);" >&2
    echo "      no released version builds SSH ramdisks." >&2
    echo "      Try upgrading in case a newer release added it:" >&2
    echo "        brew upgrade blacktop/tap/ipsw" >&2
    echo "      Otherwise follow docs/execution-plan.md §3.0:" >&2
    echo "        option B -- prebuilt ramdisk (UNVERIFIED: dora2-iOS has 0 public repos)" >&2
    echo "        option C -- manual pipeline (iExtractor + img4tool + iBoot64Patcher)" >&2
    echo "      (log: $LOGFILE)" >&2
    exit 1
fi

# ------------------------------------------------------------------
# 3. Success: refresh env.sh, overriding TR4MPASS_RAMDISK_PATH.
#    (This replaces the stock erase ramdisk: that dmg stays on disk as
#    patching base material, but the pipeline now boots the ssh ramdisk.)
# ------------------------------------------------------------------
require_file ssh-ramdisk "$RAMDISK_OUT"
ENV_SH="$OUT_DIR/env.sh"
{
    if [ -f "$ENV_SH" ]; then
        grep -v '^export TR4MPASS_RAMDISK_PATH=' "$ENV_SH"
    else
        echo "# Generated by scripts/build_ramdisk_ssh.sh."
        echo "# NOTE: run scripts/fetch_images.sh first to fill in the other"
        echo "# TR4MPASS_* image paths (iBSS/iBEC/devicetree/trustcache)."
    fi
    echo "# SSH ramdisk built by scripts/build_ramdisk_ssh.sh -- replaces the"
    echo "# stock erase ramdisk (which remains the patching base)."
    echo "export TR4MPASS_RAMDISK_PATH=\"$RAMDISK_OUT\""
} > "$ENV_SH"

SIZE=$(wc -c < "$RAMDISK_OUT" | tr -d ' ')
SHA=$(shasum -a 256 "$RAMDISK_OUT" | awk '{print $1}')
printf '  %-40s %10s bytes  sha256=%s\n' "$(basename "$RAMDISK_OUT")" "$SIZE" "$SHA"

info "ssh ramdisk: $RAMDISK_OUT"
info "env file:    $ENV_SH  (source it: . $ENV_SH -- TR4MPASS_RAMDISK_PATH now points at the ssh ramdisk)"
