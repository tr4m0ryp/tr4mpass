#!/bin/bash
# pongo_load_ramdisk.sh -- drive the live pongoOS shell: upload the
# ramdisk (+ optional KPF), set boot-args, and boot xnu.
#
# Preconditions:
#   - device is sitting in the pongoOS shell (ra1npoc with the tr4mpass
#     pongoOS patch run on a fresh DFU: shell stays alive, E28)
#   - "$(state_dir)/images/env.sh" exists (fetch_images.sh)
#   - "$(state_dir)/tools/pongo_upload" built (build_ra1npoc.sh also
#     builds it -- see tools/pongo_upload.c for the shell protocol)
#
# Sequence (each step via pongo_upload):
#   upload ramdisk  -> cmd "ramdisk"   (pongo consumes the xfer buffer)
#   upload kpf      -> cmd "modload"   (optional, --kpf)
#   cmd "xargs ..." (optional, --bootargs)
#   cmd "bootx"     (unless --no-boot)
#
# Usage:
#   pongo_load_ramdisk.sh [--ramdisk PATH] [--kpf PATH] [--bootargs STR]
#                         [--no-boot]
#   --ramdisk overrides TR4MPASS_RAMDISK_PATH (e.g. the slim SSH
#   ramdisk, TR4MPASS_SSHRD_PATH, for the Path A boot).

set -u
set -o pipefail

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
source "$SELF_DIR/lib.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }

KPF=""
BOOTARGS=""
DO_BOOT=1
RAMDISK=""
while [ $# -gt 0 ]; do
    case "$1" in
        --ramdisk)  [ $# -ge 2 ] || fail "--ramdisk needs a value"; RAMDISK="$2"; shift 2 ;;
        --kpf)      [ $# -ge 2 ] || fail "--kpf needs a value"; KPF="$2"; shift 2 ;;
        --bootargs) [ $# -ge 2 ] || fail "--bootargs needs a value"; BOOTARGS="$2"; shift 2 ;;
        --no-boot)  DO_BOOT=0; shift ;;
        *) fail "unknown option: $1" ;;
    esac
done

UPLOADER="$(state_dir)/tools/pongo_upload"
require_file "pongo_upload" "$UPLOADER"

ENV_SH="$(state_dir)/images/env.sh"
require_file "env.sh" "$ENV_SH"
# shellcheck disable=SC1090
source "$ENV_SH"
if [ -z "$RAMDISK" ]; then
    require_env_var TR4MPASS_RAMDISK_PATH
    RAMDISK="$TR4MPASS_RAMDISK_PATH"
fi
require_file "ramdisk" "$RAMDISK"

RAMDISK_SZ=$(stat -f %z "$RAMDISK")
if [ "$RAMDISK_SZ" -gt $((128 * 1024 * 1024)) ]; then
    fail "ramdisk is $RAMDISK_SZ bytes; pongoOS UPLOADSZ_MAX is 128 MiB"
fi

LOG="$(log_dir)/pongo-load-$(date +%Y%m%d-%H%M%S).log"
step() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }

# cmdq: send a command ONLY when the shell is idle (0xA1/2 status
# poll -- official client flow), then stream its stdout until the
# expect substring appears. Resending while busy is what panicked
# pongo's USB core twice (E32); the tool refuses to do it.
run() {
    step "cmd: $1"
    "$UPLOADER" cmdq "$1" 2>&1 | tee -a "$LOG" || fail "cmd '$1' failed"
}

run_confirmed() {
    step "cmd (confirmed): $1"
    local i resp
    for i in 1 2 3; do
        resp="$("$UPLOADER" cmdq "$1" "$2" 2>&1 | tr -d '\r')"
        echo "$resp" | tee -a "$LOG"
        case "$resp" in
            *"$2"*) step "confirmed: '$2' seen"; return 0 ;;
        esac
    done
    fail "'$1' never confirmed '$2'"
}

step "upload ramdisk ($RAMDISK_SZ bytes)"
"$UPLOADER" upload "$RAMDISK" 2>&1 | tee -a "$LOG" \
    || fail "ramdisk upload failed"

run "ramdisk"

if [ -n "$KPF" ]; then
    require_file "kpf" "$KPF"
    step "upload kpf"
    "$UPLOADER" upload "$KPF" 2>&1 | tee -a "$LOG" \
        || fail "kpf upload failed"
    run "modload"
fi

if [ -n "$BOOTARGS" ]; then
    run_confirmed "xargs $BOOTARGS" "set xnu boot arg cmdline"
fi

if [ "$DO_BOOT" -eq 1 ]; then
    # xfb: hand the framebuffer to xnu so -v prints the verbose boot
    # (BSD root: ... / Still waiting for root device) on screen --
    # without it the screen goes black after 'Booting' and the only
    # verbose output has nowhere to go (no serial console on the device).
    step "cmd: xfb"
    "$UPLOADER" cmdq "xfb" 2>&1 | tee -a "$LOG" || true
    step "bootx"
    # send bootx, then keep draining stdout: pongo prints the preboot
    # hook / KPF patchset / mount messages as it boots; the USB
    # interface dies once xnu takes over (0xe00002c0 -- expected).
    "$UPLOADER" cmdq "bootx" 2>&1 | tee -a "$LOG" || true
    for i in 1 2 3 4 5 6 7 8 9 10; do
        "$UPLOADER" drain 2>>"$LOG" | tee -a "$LOG" >/dev/null 2>&1 || true
        sleep 1
    done
    step "bootx sent -- console captured above; watch the device screen"
else
    step "--no-boot: shell left alive, ramdisk loaded"
fi