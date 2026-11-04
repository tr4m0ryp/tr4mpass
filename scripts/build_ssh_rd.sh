#!/usr/bin/env bash
#
# build_ssh_rd.sh -- Path A phase 3: build the custom SSH ramdisk.
#
# Inputs (verified 2026-11-01, journal E31/E32):
#   images/018-18125-090.dmg   IM4P(rdsk); payload is a RAW HFSX volume
#                              already decrypted -- no GID key needed
#                              (E31: extracts + mounts directly).
#   tools/sshtars/ssh.tar.gz   verygenericname/SSHRD_Script ssh kit
#                              (BSD-3, vendored; README-tr4mpass.md).
#
# Recipe (mirrors SSHRD_Script sshrd.sh, adapted to the tr4mpass
# pongo-upload boot path: no SHSH/IM4M signing needed, pongoOS+KPF
# boots the rdsk without a personalized manifest):
#   1. ipsw img4 im4p extract            -> raw HFSX volume image
#   2. hdiutil attach rw                 -> copy in the ssh kit
#   3. overlay kit dylibs (stock fs has no libncurses/libresolv),
#      trim unused heavy files, verify every kit binary's dylib deps
#      resolve on the result
#   4. detach, REBUILD as a 48 MiB raw HFS volume (hdiutil create
#      -srcfolder, SSHRD_Script's modern-Darwin recipe): pongoOS
#      alloc_static copies the WHOLE ramdisk into a 64 MiB hardcap
#      region (mm.c alloc_static_hardcap) that must survive into xnu,
#      and the stock volume is ~78 MiB used -- it can never boot.
#      Seen live in E32c: 'panic: ran out of static region' AFTER KPF
#      applied the full patchset incl. 'Found ramdisk'.
#   5. wrap into IM4P(rdsk) via ipsw, verify round-trip + contents
#
# Size: slimmed volume must be <= ~56 MiB (48 MiB target, 60 MiB
# hard check); the final IM4P must stay <= 128 MiB (pongoOS
# UPLOADSZ_MAX, usbloader.c) -- script fails loudly if exceeded.
#
# Output: "$(state_dir)/images/ramdisk_ssh.dmg" (IM4P rdsk); env.sh
# gets TR4MPASS_SSHRD_PATH.

set -u

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
source "$SELF_DIR/lib.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "[ssh-rd] $*"; }

SRC_IM4P="$(state_dir)/images/018-18125-090.dmg"
KIT_TAR="$SELF_DIR/../tools/sshtars/ssh.tar.gz"
OUT="$(state_dir)/images/ramdisk_ssh.dmg"
WORK="$(state_dir)/work_sshrd"
MNT="$(state_dir)/mnt_sshrd"

require_file "stock ramdisk im4p" "$SRC_IM4P"
require_file "ssh kit tar" "$KIT_TAR"
command -v ipsw >/dev/null || fail "ipsw not found (brew install ipsw)"
command -v hdiutil >/dev/null || fail "hdiutil not found"
command -v otool >/dev/null || fail "otool not found (xcode CLT)"

rm -rf "$WORK" "$MNT"
mkdir -p "$WORK" "$MNT"

info "extracting IM4P payload (raw HFSX volume image)"
ipsw img4 im4p extract "$SRC_IM4P" -o "$WORK/rd.hfs" >/dev/null \
    || fail "im4p extract failed"
RAW_SZ=$(stat -f %z "$WORK/rd.hfs")
info "raw fs: $RAW_SZ bytes"

# hdiutil only mounts a bare HFS image if it is named .dmg (raw disk
# image); the extension matters, content does not.
cp "$WORK/rd.hfs" "$WORK/rd.dmg"

info "attaching rw at $MNT"
hdiutil attach -mountpoint "$MNT" -owners off "$WORK/rd.dmg" \
    >/dev/null || fail "attach failed"
DETACHED=0
detach() {
    if [ "$DETACHED" -eq 0 ]; then
        hdiutil detach -force "$MNT" >/dev/null 2>&1 || true
        DETACHED=1
    fi
}
trap detach EXIT
R="$MNT"

info "staging + trimming the kit locally (volume has limited free space)"
KIT="$WORK/kit"
mkdir -p "$KIT"
tar xzf "$KIT_TAR" -C "$KIT" || fail "kit untar failed"
rm -rf "$KIT/usr/share/locale" "$KIT/usr/share/nano" \
       "$KIT/usr/share/terminfo" "$KIT/usr/trollstore"
rm -f  "$KIT/usr/share/misc/magic.mgc"
rm -f  "$KIT/usr/bin/nano" "$KIT/usr/bin/rnano" "$KIT/usr/bin/sqlite3" \
       "$KIT/usr/bin/binbag" "$KIT/usr/bin/screen" \
       "$KIT/usr/bin/trollstoreinstaller"
KIT_SZ=$(du -sk "$KIT" | cut -f1)
FREE_KB=$(df -k "$MNT" | tail -1 | awk '{print $4}')
info "trimmed kit: ${KIT_SZ} KiB; volume free: ${FREE_KB} KiB"
if [ "$KIT_SZ" -ge "$FREE_KB" ]; then
    fail "trimmed kit (${KIT_SZ} KiB) does not fit (${FREE_KB} KiB free)"
fi
# snapshot the stock /usr/lib contents BEFORE the kit copy: the later
# dylib purge must delete only stock libs, never kit-provided ones
ls "$R/usr/lib" > "$WORK/stock_libs.txt" || fail "stock lib snapshot failed"
cp -Rp "$KIT/." "$R/" || fail "kit copy failed"
rm -rf "$KIT"
# note: the kit's own dylibs went into $R/usr/lib with the copy (the
# stock fs lacks libncurses/libresolv, the kit provides them)

printf 'tr4mpass ssh ramdisk\nmount rootfs rw: /usr/bin/mount_filesystems (disk0s1s1 -> /mnt1)\n' \
    > "$R/usr/share/motd.tr4mpass" 2>/dev/null || true

# Launch path: launchd runs the STOCK com.apple.restored_external.plist
# (one of the 4 stock LaunchDaemons, points at
# /usr/local/bin/restored_external); the kit's tar OVERWROTE that path
# with its own restored_external, which execl's
# /usr/local/bin/dropbear on the USB mux and runs
# /bin/bash /usr/bin/mount_filesystems. No extra plist needed.

# SLIMMING: the static-region cap is 64 MiB (pongoOS alloc_static,
# mm.c: alloc_static_hardcap); the whole ramdisk is copied there to
# survive into xnu, so the volume must be < ~56 MiB. The boot kernel
# comes from the device's own APFS and is KPF-patched (seen live:
# 'Invoking preboot hook' -> 'Found ramdisk' patchset) -- only the SSH
# kit + mount tooling must remain.
info "SLIMMING: remove restore-only content"
rm -rf "$R/usr/standalone"                       # periph firmware, 27MB
# E48 lesson: mount_apfs + mount link PrivateFrameworks/APFS.framework
# and PrivateFrameworks/MediaKit.framework -- without them the rootfs
# cannot be mounted rw over SSH (the whole point of the ramdisk).
# Keep ONLY these two PrivateFrameworks, delete the rest.
find "$R/System/Library/PrivateFrameworks" -maxdepth 1 -mindepth 1 \
    ! -name 'APFS.framework' ! -name 'MediaKit.framework' \
    -exec rm -rf {} + 2>/dev/null || true
# E51/E52 lesson: apfs_sealvolume (snapshot re-seal + root-snapshot re-pin,
# the tool that moves the volume superblock's root-snapshot xid) LC_LOADs
# PrivateFrameworks/AppleFSCompression -- which lives ONLY in the dyld
# shared cache, not in the stock restore ramdisk. Copy it from the
# extracted stash (state dir survives build WORK wipes).
FW_STASH="$(state_dir)/images/fw_stash"
# TR4MPASS_SKIP_AFC=1: build a slim revert-only image without
# AppleFSCompression (mv/reboot boots don't exec apfs_sealvolume).
# Keeps the volume at the 44 MiB line the proven KPF uploads need.
if [ "${TR4MPASS_SKIP_AFC:-0}" = "1" ]; then
    info "TR4MPASS_SKIP_AFC=1: skipping AppleFSCompression embed"
elif [ -d "$FW_STASH/AppleFSCompression.framework" ]; then
    cp -Rp "$FW_STASH/AppleFSCompression.framework" \
        "$R/System/Library/PrivateFrameworks/" \
        || fail "AppleFSCompression copy failed"
else
    if [ "${TR4MPASS_SKIP_AFC:-0}" != "1" ]; then
        fail "fw_stash missing: extract AppleFSCompression from the device dyld shared cache first (see images/fw_stash/README-tr4mpass.txt)"
    fi
fi
if [ "${TR4MPASS_SKIP_AFC:-0}" != "1" ]; then
[ -e "$R/System/Library/PrivateFrameworks/AppleFSCompression.framework/AppleFSCompression" ] \
    || fail "AppleFSCompression dylib missing after copy"
fi
rm -rf "$R/System/Library/Extensions"            # disk kernel has own
for fw in CFNetwork CoreServices Foundation MobileCoreServices \
         Security SystemConfiguration IOSurface; do
    rm -rf "$R/System/Library/Frameworks/$fw.framework"
done
rm -rf "$R/System/Library/FDR" "$R/System/Library/FeatureFlags"
rm -f  "$R/usr/libexec/seputil" "$R/usr/libexec/SavageUtil" \
       "$R/usr/libexec/DumpPanic"
rm -f  "$R/usr/sbin/asr" "$R/usr/sbin/hdik" "$R/usr/sbin/mtree" \
       "$R/usr/sbin/fsck" "$R/usr/sbin/fsck_hfs" \
       "$R/usr/sbin/fsck_apfs" "$R/usr/sbin/apfs_hfs_convert" \
       "$R/usr/sbin/newfs_apfs" "$R/usr/sbin/newfs_hfs"
# E42 lesson: KEEP the binaries the 4 stock ramdisk LaunchDaemons
# reference -- their plists stay, a daemon that cannot exec dies at
# boot; the E42 boot mounted rootfs + started launchd but the device
# rebooted to iOS (userspace died). Restored: syslogd,
# diskimagesiod, PurpleReverseProxy (all small vs the 27MB firmware).
# /usr/lib: keep ONLY dylibs needed by what survives (kit binaries +
# launchd + mount tools + CoreFoundation). Verified by otool closure
# over the kit tar + sbin/mount_apfs (E33): libSystem(+sub-libs+dyld,
# implicit), libobjc.A (launchd), libutil (mount_apfs), libz/libiconv/
# liblzma/libbz2/libc++/libcompression/libarchive.2/libintl.8 (kit).
# Everything else is restore-only: libnetwork alone is 9.4 MB.
KEEP_LIST="libSystem.B.dylib libsystem_c.dylib libsystem_kernel.dylib \
            libsystem_malloc.dylib libsystem_dyld.dylib \
            libsystem_info.dylib libsystem_trace.dylib \
            libsystem_notify.dylib libsystem_asl.dylib \
            libsystem_platform.dylib libsystem_pthread.dylib \
            libsystem_coreservices.dylib libsystem_dnssd.dylib \
            libsystem_network.dylib libsystem_symptoms.dylib \
            libsystem_hosttime.dylib libsystem_m.dylib \
            libsystem_darwin.dylib libdispatch.dylib libxpc.dylib \
            libobjc.A.dylib libobjc.trie.dylib libc++abi.dylib \
            libutil.dylib libz.1.dylib libiconv.2.dylib \
            liblzma.5.dylib libbz2.1.0.dylib libc++.1.dylib \
            libcompression.dylib libarchive.2.dylib libintl.8.dylib \
            dyld libicucore.A.dylib libMobileGestalt.dylib \
            libbsm.0.dylib libenergytrace.dylib libcharset.1.dylib"
# E43: /usr/lib/dyld was NOT in the list and the purge deleted it ->
# launchd exec failed in load_machfile (dyld vnode missing) -> kernel
# SIGKILL of init -> panic "unexpected SIGKILL of init, namespace 9
# (OS_REASON_EXEC) code 0x1 (EXEC_EXIT_REASON_BAD_MACHO)". otool -L
# cannot catch this: it prints LC_LOAD_DYLIB entries, never the
# LC_LOAD_DYLINKER path. dyld is checked explicitly below.
cd "$R/usr/lib" || fail "usr/lib missing"
for f in *; do
    [ -e "$f" ] || continue
    grep -qxF "$f" "$WORK/stock_libs.txt" || continue  # kit-provided: keep
    case " $KEEP_LIST " in
        *" $f "*) ;;
        *) rm -f "$f" ;;
    esac
done
cd "$SELF_DIR" || exit 1

# re-verify every surviving binary's dylib deps now resolve on the
# SLIMMED fs (this is the check that matters -- it used to run before
# the slim-down and validated a filesystem we then deleted from)
info "verifying dylib deps resolve on the SLIMMED fs"
MISSING=""
for b in "$R"/usr/local/bin/dropbear "$R"/bin/bash "$R"/bin/launchctl \
         "$R"/usr/sbin/ioreg "$R"/usr/bin/sed "$R"/sbin/launchd \
         "$R"/sbin/mount "$R"/System/Library/Filesystems/apfs.fs/mount_apfs \
         "$R"/usr/local/bin/restored_external "$R"/usr/local/bin/dbclient \
         "$R"/usr/libexec/sftp-server "$R"/usr/bin/img4tool; do
    [ -e "$b" ] || { MISSING="$MISSING (absent $(basename "$b"))"; continue; }
    for lib in $(otool -L "$b" 2>/dev/null \
                 | awk '/dylib/{print $1}' | sed 's|.*/||'); do
        [ "$lib" = "libSystem.B.dylib" ] && continue
        if ! ls "$R/usr/lib/$lib" >/dev/null 2>&1 \
           && ! ls "$R/usr/local/lib/$lib" >/dev/null 2>&1; then
            MISSING="$MISSING ($(basename "$b"):$lib)"
        fi
    done
done
[ -z "$MISSING" ] || fail "missing dylibs after slim:$MISSING"

# E43 regression check: /usr/lib/dyld itself must survive the purge
[ -e "$R/usr/lib/dyld" ] || fail "/usr/lib/dyld missing after slim (E43 panic root cause)"
[ -x "$R/usr/lib/dyld" ] || fail "/usr/lib/dyld not executable"

# E44 lesson: transitive dylib audit. E44 boot: launchd ran, daemons
# launched, but restored_external died at dyld time -- CoreFoundation
# needed libicucore.A.dylib (purged). otool -L on leaf binaries can't
# see framework deps, so walk one level into every surviving framework
# dylib + all /usr/lib survivors and check THEIR deps too.
info "transitive dylib audit (frameworks + usr/lib, one level)"
AUDIT="$R/usr/lib"
AUDIT="$AUDIT $(find "$R/System/Library/Frameworks" -name '*.dylib' \
    -path '*Versions/A*' 2>/dev/null)"
AUDIT="$AUDIT $(find "$R/System/Library/Frameworks" -maxdepth 4 \
    -name '*.dylib' ! -path '*Versions*' 2>/dev/null)"
MISSING2=""
for b in $AUDIT; do
    [ -f "$b" ] || continue
    case "$(xxd -p -l4 "$b" 2>/dev/null)" in
        cffaedfe|feedfacf) ;;
        *) continue;;
    esac
    for lib in $(otool -L "$b" 2>/dev/null \
                 | awk '/dylib/{print $1}' | sed 's|.*/||'); do
        [ "$lib" = "libSystem.B.dylib" ] && continue
        if ! ls "$R/usr/lib/$lib" >/dev/null 2>&1 \
           && ! ls "$R/usr/local/lib/$lib" >/dev/null 2>&1; then
            MISSING2="$MISSING2 ($(basename "$b"):$lib)"
        fi
    done
done
[ -z "$MISSING2" ] || fail "missing transitive dylibs after slim:$MISSING2"

info "writing minimal mount_filesystems (no seputil/SEP; rd=md0 flow)"
cat > "$R/usr/bin/mount_filesystems" <<'MFS'
#!/bin/bash
/sbin/mount_apfs /dev/disk0s1s1 /mnt1
echo "tr4mpass: system volume mounted at /mnt1"
MFS
chmod +x "$R/usr/bin/mount_filesystems"

USED_KB=$(df -k "$R" | tail -1 | awk '{print $3}')
info "volume used after slim: ${USED_KB} KiB"
sync

# volume size: target = used rounded up to 4 MiB + 4 MiB slack,
# clamped to [44, 56] MiB -- must stay under the 64 MiB static cap
# with headroom for devicetree + misc pongo allocations.
USED_MB=$(( (USED_KB + 4095) / 4096 ))
USED_MB=$(( USED_MB * 4 + 4 ))
if [ "$USED_MB" -lt 44 ]; then USED_MB=44; fi
if [ "$USED_MB" -gt 56 ]; then USED_MB=56; fi
info "rebuilding as a ${USED_MB}MiB raw HFS volume"
# SSHRD_Script's modern-Darwin recipe (sshrd.sh:313): hdiutil create
# -srcfolder on the STILL-MOUNTED source volume, then detach after.
# 48 MiB keeps the IM4P under the 64 MiB static cap. UDRW (not
# upstream's UDZO) because the IM4P must wrap raw volume bytes.
# -copyuid root preserves ownership when privilege allows; the ramdisk
# runs everything as root, so fall back without it on failure.
hdiutil create -size ${USED_MB}m -imagekey diskimage-class=CRawDiskImage \
    -format UDRW -fs HFS+ -layout NONE -srcfolder "$MNT" \
    -copyuid root -o "$WORK/rd_small.dmg" >/dev/null \
    || hdiutil create -size ${USED_MB}m -imagekey diskimage-class=CRawDiskImage \
    -format UDRW -fs HFS+ -layout NONE -srcfolder "$MNT" \
    -o "$WORK/rd_small.dmg" >/dev/null \
    || fail "${USED_MB}MiB rebuild failed"
FINAL_SZ=$(stat -f %z "$WORK/rd_small.dmg")
info "slim raw fs: $FINAL_SZ bytes ($(( FINAL_SZ / 1024 / 1024 )) MiB)"
if [ "$FINAL_SZ" -gt $((60 * 1024 * 1024)) ]; then
    fail "fs $FINAL_SZ exceeds ~60MiB static-region budget"
fi

info "detaching"
detach
trap - EXIT

info "wrapping the SLIM volume into IM4P(rdsk)"
# TWO artifacts:
#   - RAW volume (rd_small.dmg): what xnu actually consumes as md0
#   - IM4P(rdsk) wrapper: E32c-proven to be detected by the KPF
#     ("Found ramdisk" fired with the IM4P-wrapped stock rdsk), used
#     as the default upload + archive. The KPF strings include
#     "Found old-style rdsk!" (it parses the IM4P(rdsk) container).
# NOTE: wrap $WORK/rd_small.dmg (the slim rebuild), NOT the stock
# rd.dmg -- E32 shipped a 105 MB IM4P because of exactly this wiring
# mistake waiting to happen (rd.dmg stays around from step 1).
ipsw img4 im4p create --type rdsk --compress none -o "$OUT" \
    "$WORK/rd_small.dmg" || fail "im4p create failed"
OUT_SZ=$(stat -f %z "$OUT")
info "IM4P size: $OUT_SZ bytes"
if [ "$OUT_SZ" -gt $((128 * 1024 * 1024)) ]; then
    fail "IM4P $OUT_SZ > 128 MiB pongoOS upload cap"
fi
RAW_OUT="$(state_dir)/images/ramdisk_ssh_raw.hfs"
cp "$WORK/rd_small.dmg" "$RAW_OUT"
info "raw volume copy: $RAW_OUT"

info "verify: extract the rebuilt payload and mount it"
ipsw img4 im4p extract "$OUT" -o "$WORK/verify.hfs" >/dev/null \
    || fail "verify extract failed"
cmp -s "$WORK/verify.hfs" "$WORK/rd_small.dmg" \
    || fail "rebuilt payload differs from what was written"
cp "$WORK/verify.hfs" "$WORK/verify.dmg"
hdiutil attach -readonly -mountpoint "$MNT" "$WORK/verify.dmg" >/dev/null \
    || fail "rebuilt volume does not mount"
[ -e "$MNT/usr/local/bin/dropbear" ] \
    || { hdiutil detach "$MNT"; fail "dropbear missing in rebuilt volume"; }
[ -e "$MNT/System/Library/LaunchDaemons/com.apple.restored_external.plist" ] \
    || { hdiutil detach "$MNT"; fail "stock restored_external plist missing"; }
[ -e "$MNT/usr/local/bin/restored_external" ] \
    || { hdiutil detach "$MNT"; fail "kit restored_external missing"; }
[ -e "$MNT/usr/bin/mount_filesystems" ] \
    || { hdiutil detach "$MNT"; fail "mount_filesystems missing"; }
if [ "${TR4MPASS_SKIP_AFC:-0}" != "1" ]; then
[ -e "$MNT/System/Library/PrivateFrameworks/AppleFSCompression.framework/AppleFSCompression" ] \
    || { hdiutil detach "$MNT"; fail "AppleFSCompression framework missing"; }
fi
[ -e "$MNT/System/Library/Filesystems/apfs.fs/apfs_sealvolume" ] \
    || { hdiutil detach "$MNT"; fail "apfs_sealvolume missing"; }
[ -x "$MNT/bin/bash" ] \
    || { hdiutil detach "$MNT"; fail "bash missing/not executable"; }
hdiutil detach "$MNT" >/dev/null

ENV_SH="$(state_dir)/images/env.sh"
for pair in "TR4MPASS_SSHRD_PATH|$OUT" "TR4MPASS_SSHRD_RAW_PATH|$RAW_OUT"; do
    KEY="${pair%%|*}"; VAL="${pair#*|}"
    if grep -q "$KEY" "$ENV_SH" 2>/dev/null; then
        sed -i '' 's|export '"$KEY"'=.*|export '"$KEY"'="'"$VAL"'"|' "$ENV_SH"
    else
        echo 'export '"$KEY"'="'"$VAL"'"' >> "$ENV_SH"
    fi
done

echo "OK: $OUT ($OUT_SZ bytes)"
