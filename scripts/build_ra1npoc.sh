#!/bin/bash
# build_ra1npoc.sh -- build the vendored ra1npoc loader + pongoterm.
#
# Sources: tools/ra1npoc (vendored, see tools/ra1npoc/README-tr4mpass.md
# for provenance and the tr4mpass patches).
# Outputs: "$(state_dir)/tools/ra1npoc" and "$(state_dir)/tools/pongoterm".
#
# Requirements: Xcode command line tools (xcrun clang, IOKit/CoreFoundation).
# The payload headers are regenerated from dumpfiles/ (builtin.sh), so no
# binary blobs live in git beyond the originals themselves.

set -eu

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
source "$SELF_DIR/lib.sh"

RA1NPOC_DIR="$SELF_DIR/../tools/ra1npoc"
OUT_DIR="$(state_dir)/tools"
LOG="$(log_dir)/build-ra1npoc-$(date +%Y%m%d-%H%M%S).log"

[ -d "$RA1NPOC_DIR/src" ] || { echo "FAIL: $RA1NPOC_DIR/src missing" >&2; exit 1; }
command -v xxd >/dev/null || { echo "FAIL: xxd not found (part of vim/xxd, brew install xxd)" >&2; exit 1; }

mkdir -p "$OUT_DIR"
cd "$RA1NPOC_DIR/src"

echo "[1/3] generating payload headers from dumpfiles/"
./builtin.sh > "$LOG" 2>&1

echo "[2/3] building ra1npoc (macOS host)"
xcrun -sdk macosx clang -I./include -I./ios_headers \
    -framework IOKit -framework CoreFoundation \
    main.c io/iousb.c common/common.c exploit/checkm8_arm64.c \
    -o "$OUT_DIR/ra1npoc" >> "$LOG" 2>&1
codesign -f -s - "$OUT_DIR/ra1npoc" >> "$LOG" 2>&1

echo "[3/3] building pongoterm"
make -C "$RA1NPOC_DIR/pongoOS/scripts" clean >> "$LOG" 2>&1 || true
make -C "$RA1NPOC_DIR/pongoOS/scripts" >> "$LOG" 2>&1
cp "$RA1NPOC_DIR/pongoOS/scripts/pongoterm" "$OUT_DIR/pongoterm"

require_file "ra1npoc" "$OUT_DIR/ra1npoc"
require_file "pongoterm" "$OUT_DIR/pongoterm"

echo "[4/3] building pongo_upload"
clang -O2 -Wall -o "$OUT_DIR/pongo_upload" \
    "$SELF_DIR/../tools/pongo_upload.c" \
    -framework CoreFoundation -framework IOKit >> "$LOG" 2>&1
require_file "pongo_upload" "$OUT_DIR/pongo_upload"

# Extract the checkra1n KPF (kernel patcher, Mach-O pongo module) from
# the composed blob -- E29 panic fix: bootx with a ramdisk needs the KPF
# loaded (modload) or the unpatched kernel panics.
KPF_OUT="$(state_dir)/images/kpf_checkra1n.bin"
mkdir -p "$(dirname "$KPF_OUT")"
python3 - "$RA1NPOC_DIR/dumpfiles/pongoOS" "$KPF_OUT" >> "$LOG" 2>&1 <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(data[0x3F308:0x3F308 + 0x15648])
PYEOF
require_file "kpf_checkra1n.bin" "$KPF_OUT"

echo "OK: $OUT_DIR/ra1npoc"
echo "OK: $OUT_DIR/pongoterm"
echo "OK: $OUT_DIR/pongo_upload"
echo "OK: $KPF_OUT"
