#!/bin/sh
# Wait for the device to appear in NORMAL (iOS) mode, read its ProductType,
# iOS version and build, then store them in the shared state directory.
#
# Usage: detect_ios.sh [timeout_sec]
#
# The model is never assumed: it is read from the connected device. If the
# device is in DFU mode, boot it to iOS first (unplug/replug) or use the
# caller's --model override.
set -u

TIMEOUT="${1:-180}"
STATE_DIR="${TR4MPASS_STATE_DIR:-$HOME/.cache/tr4mpass}"
START=$(date +%s)

mkdir -p "$STATE_DIR" || {
    echo "cannot create state directory: $STATE_DIR" >&2
    exit 1
}

echo "Waiting for the device in NORMAL mode (unplug/replug the cable now)..."

while :; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - START))
    if [ "$ELAPSED" -ge "$TIMEOUT" ]; then
        echo "TIMEOUT after ${TIMEOUT}s: device never showed up in normal mode." >&2
        exit 1
    fi

    if idevice_id -l 2>/dev/null | grep -q .; then
        VER=$(ideviceinfo -k ProductVersion 2>/dev/null)
        BUILD=$(ideviceinfo -k BuildVersion 2>/dev/null)
        MODEL=$(ideviceinfo -k ProductType 2>/dev/null)
        BOARD=$(ideviceinfo -k BoardConfig 2>/dev/null)
        if [ -z "${BOARD:-}" ]; then
            BOARD=$(ideviceinfo -k HardwareModel 2>/dev/null | tr '[:upper:]' '[:lower:]')
        fi
        if [ -n "${VER:-}" ] && [ -n "${BUILD:-}" ] && [ -n "${MODEL:-}" ]; then
            echo "DETECTED version=${VER} build=${BUILD} model=${MODEL} board=${BOARD:-unknown}"
            printf '%s\n' "$VER"   > "$STATE_DIR/ios_version.txt"
            printf '%s\n' "$BUILD" > "$STATE_DIR/ios_build.txt"
            printf '%s\n' "$MODEL" > "$STATE_DIR/ios_model.txt"
            printf '%s\n' "${BOARD:-}" > "$STATE_DIR/ios_board.txt"
            exit 0
        fi
    fi

    sleep 2
done
