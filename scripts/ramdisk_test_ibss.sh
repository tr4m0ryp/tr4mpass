#!/bin/bash
#
# ramdisk_test_ibss.sh -- Path A step-3 smoke test (USB transition only).
#
# What it does, end to end:
#   1. Detect the installed iOS version/model from the device (normal mode,
#      ideviceinfo). If the device is in DFU instead, print the manual
#      unplug/replug instruction and wait for it to boot iOS.
#   2. Resolve the matching IPSW URL (ipsw.me) and pull ONLY the iBSS
#      images via HTTP Range requests (a few hundred KB, no 2+ GB
#      download).
#   3. Send each iBSS variant to the pwned DFU device via irecovery.
#   4. Watch the USB PID: 0x1227 (DFU) -> 0x1281 (recovery) proves the
#      transition survives on Apple Silicon USB-C direct connection --
#      the exact risk palera1n documents ("may require manual
#      unplugging and replugging of the lightning cable after checkm8").
#
# This script never touches the checkm8 stage: it ASSUMES the device is
# in pwned DFU ("PWND:[checkm8]" in the serial). Re-pwn first if not
# (./tr4mpass -v).
#
# Requirements: irecovery, idevice_id/ideviceinfo (libimobiledevice),
# python3. All non-fatal details go to stderr; the final line is
# PASS/FAIL so the script can be chained.
#
# Usage: scripts/ramdisk_test_ibss.sh [--model <ProductType>] [--keep-images]
#
# --model is optional; without it the model is read from
# $TR4MPASS_STATE_DIR/ios_model.txt (written by detect_ios.sh). The script
# works with any ProductType: it discovers the iBSS variants present in
# the IPSW instead of assuming a specific board.
set -u

DEVICE_MODEL=""
KEEP_IMAGES=0
STATE_DIR="${TR4MPASS_STATE_DIR:-$HOME/.cache/tr4mpass}"
WORKDIR="${TR4MPASS_IMAGES_DIR:-$STATE_DIR/images}"

while [ $# -gt 0 ]; do
    case "$1" in
        --model)       DEVICE_MODEL="$2"; shift 2 ;;
        --device)      DEVICE_MODEL="$2"; shift 2 ;;  # backward-compatible alias
        --keep-images) KEEP_IMAGES=1;     shift ;;
        -h|--help)     sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "[iBSS-test] $*"; }

command -v irecovery    >/dev/null 2>&1 || fail "irecovery not found (brew install libirecovery)"
command -v idevice_id   >/dev/null 2>&1 || fail "idevice_id not found (brew install libimobiledevice)"
command -v python3      >/dev/null 2>&1 || fail "python3 not found"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ------------------------------------------------------------------
# 0. Preconditions: pwned DFU right now?
# ------------------------------------------------------------------
PWND="$(ioreg -p IOUSB -l -w0 2>/dev/null | grep -i 'USB Serial Number' | grep -c 'PWND:' || true)"
if [ "$PWND" -eq 0 ]; then
    info "device is NOT in pwned DFU. Put it in DFU and run ./tr4mpass -v first."
    exit 1
fi
info "device is in pwned DFU (serial contains PWND:)"

# ------------------------------------------------------------------
# 1. Installed iOS version/model. The device must boot iOS for
#    ideviceinfo; ask for the cable replug if it is still in DFU.
# ------------------------------------------------------------------
VERSION_FILE="$STATE_DIR/ios_version.txt"
BUILD_FILE="$STATE_DIR/ios_build.txt"
MODEL_FILE="$STATE_DIR/ios_model.txt"
if [ -f "$VERSION_FILE" ] && [ -f "$BUILD_FILE" ]; then
    info "reusing cached detection: $(cat "$VERSION_FILE") ($(cat "$BUILD_FILE"))"
else
    info "device must boot iOS once so we can read its version."
    info ">>> UNPLUG and REPLUG the cable now (device boots to iOS)."
    "$SCRIPT_DIR/detect_ios.sh" 300 || \
        fail "could not detect iOS version (no normal-mode device seen)"
fi

IOS_VERSION="$(cat "$VERSION_FILE")"
IOS_BUILD="$(cat "$BUILD_FILE")"
if [ -z "$DEVICE_MODEL" ] && [ -f "$MODEL_FILE" ]; then
    DEVICE_MODEL="$(cat "$MODEL_FILE")"
fi
[ -n "$DEVICE_MODEL" ] || fail "no device model detected -- run scripts/detect_ios.sh or pass --model <ProductType>"
info "iOS ${IOS_VERSION} (build ${IOS_BUILD}) on ${DEVICE_MODEL}"

# ------------------------------------------------------------------
# 2. Pull iBSS variants via HTTP Range.
# ------------------------------------------------------------------
mkdir -p "$WORKDIR"
IPSW_URL="$(python3 "$SCRIPT_DIR/ipsw_pull.py" url-for "$DEVICE_MODEL" "$IOS_BUILD")" \
    || fail "could not resolve IPSW URL for build $IOS_BUILD"
info "IPSW: $IPSW_URL"

# An IPSW can carry several iBSS board variants (for example the
# dual-sourced A9). The device's board selects the right one; a wrong-board
# image is simply not booted, so fetch them all and try each below.
python3 "$SCRIPT_DIR/ipsw_pull.py" fetch "$IPSW_URL" \
        "Firmware/dfu/iBSS.*\.im4p" "$WORKDIR" \
    || fail "iBSS extraction failed"
IBSS_FILES="$(ls "$WORKDIR"/iBSS*.im4p 2>/dev/null || true)"
[ -n "$IBSS_FILES" ] || fail "no iBSS file produced"
info "iBSS images: $(printf '%s ' $IBSS_FILES)"

# ------------------------------------------------------------------
# 3. Device state check + upload.
# ------------------------------------------------------------------
sleep 1
PWND="$(ioreg -p IOUSB -l -w0 2>/dev/null | grep -i 'USB Serial Number' | grep -c 'PWND:' || true)"
if [ "$PWND" -eq 0 ]; then
    info "device left pwned DFU (it booted iOS for detection)."
    info ">>> Put it back in DFU and re-run ./tr4mpass -v, then re-run this script."
    exit 2
fi

watch_recovery() {
    # $1 = seconds to watch
    START=$(date +%s)
    while :; do
        NOW=$(date +%s)
        [ $((NOW - START)) -ge "$1" ] && return 1
        if ioreg -p IOUSB -l -w0 2>/dev/null | grep -q '"idProduct" = 0x1281'; then
            return 0
        fi
        sleep 1
    done
}

try_upload() {
    # $1 = image path. Sends it and watches for the DFU->recovery flip.
    info "uploading $(basename "$1") to pwned DFU..."
    irecovery -f "$1" 2>&1 | sed 's/^/[irecovery] /'
    sleep 2
    watch_recovery 20 && return 0
    # Not booted: send a reset to get back to a clean DFU before retry.
    irecovery -c reset >/dev/null 2>&1 || true
    sleep 2
    return 1
}

RECOVERY_SEEN=0
for IMG in $IBSS_FILES; do
    [ -f "$IMG" ] || continue
    if try_upload "$IMG"; then
        RECOVERY_SEEN=1
        break
    fi
    info "$(basename "$IMG") did not boot (wrong board variant?), trying next."
done

RESULT="FAIL"
if [ "$RECOVERY_SEEN" -eq 1 ]; then
    RESULT="PASS"
    info "recovery mode reached (PID 0x1281) -- USB-C transition OK."
    info "the iBSS booted; a full path_a run would now upload iBEC/devicetree/ramdisk."
else
    info "no recovery mode within 60 s -- the transition DID NOT happen."
fi

if [ "$KEEP_IMAGES" -eq 0 ]; then
    rm -f "$WORKDIR"/iBSS*.im4p
fi

echo "$RESULT"
[ "$RESULT" = "PASS" ]
