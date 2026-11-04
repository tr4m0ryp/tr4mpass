#!/bin/bash
# boot_sshrd.sh -- canonical one-shot SSH-ramdisk boot through pongoOS.
#
# This is the E64+ recipe (see docs/apple-silicon-usbc/journal/ssh-ramdisk.md,
# E60-E64 sections) as a runnable script, replacing the ad-hoc inline
# chains used during E63-E68. KPF is ALWAYS uploaded, using the
# pre-padded artifact:
#   kpf_checkra1n_1k.bin (88 064 B) -- padded to a 1 KiB multiple but
#   NOT a 16 KiB multiple: pongoOS completes an xfer on a *short* final
#   OUT chunk (the terminator), so 16 KiB-alignment removes it and
#   modload reports "short read" forever. Trailing zeros beyond the
#   declared size are ignored (xfer sized by newsz).
#
# Preconditions:
#   - device fresh in DFU (USB 0x1227)
#   - "$(state_dir)/images/{kpf_checkra1n_1k.bin,ramdisk_ssh_raw.hfs}"
#     exist (fetch/build scripts)
#   - "$(state_dir)/tools/{ra1npoc,pongo_upload}" built
#     (build_ra1npoc.sh) and "$SELF_DIR/ssh_expect22.sh" present.
#
# Sequence (each step via pongo_upload, logs under "$(log_dir)"):
#   ra1npoc (pwn DFU) -> pongo 0x4141 wait
#   upload KPF        -> cmd modload
#   upload ramdisk    -> cmd ramdisk
#   cmd "xargs rootdev=md0 rd=md0 -v wdt=-1 trm_default=0"
#   cmd xfb -> cmd bootx
#   -> settle 55 s -> iproxy 2223 -> ssh probes ("echo SHELL_OK")
#
# Markers on stdout (grep-able, same as the E64-E68 logs):
#   |NO_DFU| |RA1NPOC_DONE| |PONGO_SEEN| |KPF_OK| |RD_OK| |BOOTX_SENT|
#   TRYn: <probe result>; loud aborts: |KPF_FAIL| |RD_FAIL| |SHELL_FAIL|
#
# Usage: boot_sshrd.sh [attempts(=4)]
set -u
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib.sh
source "$SELF_DIR/lib.sh"

STATE="$(state_dir)"
LOG_ROOT="$(log_dir)"
STAMP="$(date +%Y%m%d-%H%M%S)"
for F in "$STATE/tools/ra1npoc" "$STATE/tools/pongo_upload" \
         "$STATE/images/kpf_checkra1n_1k.bin" "$STATE/images/ramdisk_ssh_raw.hfs"; do
    require_file "boot chain prerequisite" "$F"
done
U="$STATE/tools/pongo_upload"
KPF="$STATE/images/kpf_checkra1n_1k.bin"
RD="$STATE/images/ramdisk_ssh_raw.hfs"
TRIES="${1:-4}"

# 1. DFU only (IOUSB, product id 0x1227 decimal 4647).
ioreg -p IOUSB -l -w0 2>/dev/null | grep -q "4647" || { echo "|NO_DFU|"; exit 1; }
pkill -f iproxy 2>/dev/null; sleep 1

# 2. Exploit -> pongoOS shell.
"$STATE/tools/ra1npoc" > "$LOG_ROOT/boot_sshrd-$STAMP.ra1npoc.log" 2>&1
echo "|RA1NPOC_DONE|"
sleep 2
for i in $(seq 1 60); do
    ioreg -p IOUSB -l -w0 2>/dev/null | grep -q "16705" && { echo "|PONGO_SEEN|"; break; }
    sleep 1
done
ioreg -p IOUSB -l -w0 2>/dev/null | grep -q "16705" || { echo "|PONGO_TIMEOUT|"; exit 1; }

# 3. KPF first (padded), then modload.
"$U" upload "$KPF" > "$LOG_ROOT/boot_sshrd-$STAMP.kpf.log" 2>&1 || { echo "|KPF_FAIL|"; exit 1; }
grep -q "+0 pad" "$LOG_ROOT/boot_sshrd-$STAMP.kpf.log" && echo "|KPF_OK|"
"$U" cmd modload > "$LOG_ROOT/boot_sshrd-$STAMP.modload.log" 2>&1
echo "|MODLOAD_SENT|"

# 4. Ramdisk + boot-args + xfb + bootx.
"$U" upload "$RD" > "$LOG_ROOT/boot_sshrd-$STAMP.rd.log" 2>&1 || { echo "|RD_FAIL|"; exit 1; }
grep -q "+0 pad" "$LOG_ROOT/boot_sshrd-$STAMP.rd.log" && echo "|RD_OK|"
"$U" cmd ramdisk > "$LOG_ROOT/boot_sshrd-$STAMP.rd_cmd.log" 2>&1
"$U" cmd "xargs rootdev=md0 rd=md0 -v wdt=-1 trm_default=0" \
    > "$LOG_ROOT/boot_sshrd-$STAMP.xargs.log" 2>&1
"$U" cmd xfb > "$LOG_ROOT/boot_sshrd-$STAMP.xfb.log" 2>&1
"$U" cmd bootx > "$LOG_ROOT/boot_sshrd-$STAMP.bootx.log" 2>&1
echo "|BOOTX_SENT|"

# 5. Wait for the ramdisk's dropbear, then probe.
sleep 55
pkill -f iproxy 2>/dev/null; sleep 1
(iproxy 2223 22 >/dev/null 2>&1 &)
sleep 4
for r in $(seq 1 "$TRIES"); do
    OUT=$(timeout 40 "$SELF_DIR/ssh_expect22.sh" 'echo SHELL_OK' 2>&1 | tail -1)
    echo "TRY${r}: ${OUT}"
    case "$OUT" in *SHELL_OK*) echo "|SHELL_OK|"; exit 0;; esac
    sleep 8
done
echo "|SHELL_FAIL|"
exit 1
