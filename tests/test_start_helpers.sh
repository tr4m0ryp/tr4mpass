#!/usr/bin/env bash
# test_start_helpers.sh -- Regression tests for the guided wrapper.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TEST_TMP_ROOT="${TR4MPASS_TEST_TMP_ROOT:-/tmp/opencode}"
mkdir -p "$TEST_TMP_ROOT"
TMP_DIR="$(mktemp -d "$TEST_TMP_ROOT/tr4mpass-wrapper-test.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

PASS_COUNT=0

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf 'PASS: %s\n' "$1"
}

assert_eq() {
    local expected="$1"
    local actual="$2"
    local label="$3"
    [ "$expected" = "$actual" ] || fail "$label (expected '$expected', got '$actual')"
    pass "$label"
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local label="$3"
    case "$haystack" in
        *"$needle"*) pass "$label" ;;
        *) fail "$label (missing '$needle')" ;;
    esac
}

assert_not_contains() {
    local haystack="$1"
    local needle="$2"
    local label="$3"
    case "$haystack" in
        *"$needle"*) fail "$label (unexpected '$needle')" ;;
        *) pass "$label" ;;
    esac
}

mkdir -p "$TMP_DIR/bin"
export LSUSB_LOG="$TMP_DIR/lsusb.log"
export BINARY_LOG="$TMP_DIR/binary.log"
export SUDO_LOG="$TMP_DIR/sudo.log"

cat >"$TMP_DIR/bin/lsusb" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$LSUSB_LOG"
if [ "${1:-}" = "-v" ]; then
    printf '%s\n' "${LSUSB_VERBOSE_OUTPUT:-}"
    exit 0
fi
printf '%s\n' "${LSUSB_LISTING:-}"
STUB

cat >"$TMP_DIR/bin/tr4mpass" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$BINARY_LOG"
printf '%s\n' "${BINARY_OUTPUT:-}"
STUB

cat >"$TMP_DIR/bin/sudo" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$SUDO_LOG"
exit 99
STUB

cat >"$TMP_DIR/bin/system_profiler" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "${SYSTEM_PROFILER_OUTPUT:-}"
STUB

cat >"$TMP_DIR/bin/pkg-config" <<'STUB'
#!/usr/bin/env bash
exit 0
STUB

chmod +x "$TMP_DIR/bin/lsusb" "$TMP_DIR/bin/tr4mpass" "$TMP_DIR/bin/sudo" \
         "$TMP_DIR/bin/system_profiler" "$TMP_DIR/bin/pkg-config"
export PATH="$TMP_DIR/bin:$PATH"

RED='' GREEN='' YELLOW='' CYAN='' BOLD='' RESET=''
SCRIPT_DIR="$ROOT_DIR"
BINARY="$TMP_DIR/bin/tr4mpass"
DETECTED_OS=linux
DEVICE_MODE=dfu
DEVICE_USB_PID=''

# shellcheck source=../start-helpers.sh
. "$ROOT_DIR/start-helpers.sh"

# 1. Classic DFU PID recognition.
: >"$LSUSB_LOG"
export LSUSB_LISTING='Bus 001 Device 002: ID 05ac:1227 Apple Inc. DFU'
check_dfu
assert_eq '0x1227' "${DEVICE_USB_PID:-}" 'check_dfu recognizes classic DFU PID'

# 2. KIS PID recognition.
export LSUSB_LISTING='Bus 001 Device 003: ID 05ac:1881 Apple Inc. Debug USB'
check_dfu
assert_eq '0x1881' "${DEVICE_USB_PID:-}" 'check_dfu recognizes KIS PID'

# 3. Unrelated Apple PID is not treated as DFU-like.
export LSUSB_LISTING='Bus 001 Device 004: ID 05ac:12a8 Apple Inc. iPhone'
if check_dfu; then
    fail 'check_dfu rejects an unrelated Apple PID'
fi
assert_eq '' "${DEVICE_USB_PID:-}" 'check_dfu clears an unrelated Apple PID'

# macOS uses system_profiler rather than lsusb.
export SYSTEM_PROFILER_OUTPUT='  iPhone:
      Product ID: 0x1881
      Vendor ID: 0x05ac (Apple Inc.)'
DETECTED_OS=macos
if ! check_dfu; then
    fail 'check_dfu recognizes KIS PID on macOS'
fi
assert_eq '0x1881' "${DEVICE_USB_PID:-}" 'check_dfu recognizes KIS PID on macOS'
DETECTED_OS=linux

# 4. KIS parsing uses KIS-aware binary output, never classic DFU descriptors.
: >"$LSUSB_LOG"
: >"$BINARY_LOG"
export LSUSB_LISTING='Bus 001 Device 003: ID 05ac:1881 Apple Inc. Debug USB'
export BINARY_OUTPUT='--- Device Information ---
  Product Type:     iPhone17,1
  iOS Version:      26.6.2
  Serial:           F2LTESTDEVICE
  IMEI:             352000123456789
  CPID:             0x8120
  ECID:             0x000A7DE22260201E
  Chip Name:        A16
  checkm8 vuln:     NO
  DFU Mode:         YES
  Transport:        KIS DFU (Debug USB)'
DEVICE_MODE=dfu
DEVICE_USB_PID=0x1881
parse_device_info
assert_eq '0x8120' "$DEV_CPID" 'KIS parsing reads CPID from binary output'
assert_eq '0x000A7DE22260201E' "$DEV_ECID" 'KIS parsing reads ECID from binary output'
assert_eq 'KIS DFU (Debug USB)' "$DEV_DFU" 'KIS parsing reports the KIS mode'
assert_not_contains "$(cat "$LSUSB_LOG")" '-v -d 05ac:1227' \
    'KIS parsing avoids classic DFU descriptor reads'

# 5. Permission diagnostics are actionable and never invoke sudo themselves.
NODE="$TMP_DIR/non-writable-apple-node"
: >"$NODE"
chmod 0444 "$NODE"
: >"$SUDO_LOG"
export APPLE_USB_NODE="$NODE"
permission_output="$(check_usb_permissions 2>&1 || true)"
assert_contains "$permission_output" 'contrib/udev/99-tr4mpass.rules' \
    'permission diagnostic names the repository rule'
assert_contains "$permission_output" 'udevadm control --reload-rules' \
    'permission diagnostic explains how to reload udev'
assert_contains "$permission_output" 'udevadm trigger' \
    'permission diagnostic explains how to trigger the rule'
assert_contains "$permission_output" '1227, 1281, 1881' \
    'permission diagnostic identifies every supported Apple PID'
assert_eq '' "$(cat "$SUDO_LOG")" 'permission diagnostic does not run sudo automatically'

# 6. The experimental KIS transition remains an explicit opt-in.
DEVICE_USB_PID=0x1881
validate_kis_transition_args --detect-only
validate_kis_transition_args --dry-run
validate_kis_transition_args --experimental-kis-transition
if refusal_output="$(validate_kis_transition_args 2>&1)"; then
    fail 'KIS transition refuses a normal run without explicit opt-in'
fi
assert_contains "$refusal_output" './start.sh --experimental-kis-transition' \
    'KIS refusal prints the exact opt-in command'
pass 'KIS transition requires explicit opt-in for normal runs'

# The driver must enforce the same gate before invoking a normal run.
DRIVER_DIR="$TMP_DIR/driver"
mkdir -p "$DRIVER_DIR"
cp "$ROOT_DIR/start.sh" "$ROOT_DIR/start-helpers.sh" "$DRIVER_DIR/"
cat >"$DRIVER_DIR/tr4mpass" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$DRIVER_BINARY_LOG"
printf '%s\n' "${BINARY_OUTPUT:-}"
STUB
chmod +x "$DRIVER_DIR/tr4mpass"
export DRIVER_BINARY_LOG="$TMP_DIR/driver-binary.log"
: >"$DRIVER_BINARY_LOG"
export LSUSB_LISTING='Bus 001 Device 003: ID 05ac:1881 Apple Inc. Debug USB'
WRITABLE_NODE="$TMP_DIR/writable-apple-node"
: >"$WRITABLE_NODE"
chmod 0600 "$WRITABLE_NODE"
set +e
driver_output="$(printf '\n' | env APPLE_USB_NODE="$WRITABLE_NODE" \
    DRIVER_BINARY_LOG="$DRIVER_BINARY_LOG" BINARY_OUTPUT="$BINARY_OUTPUT" \
    "$DRIVER_DIR/start.sh" 2>&1)"
driver_rc=$?
set -e
assert_eq '1' "$driver_rc" 'start.sh stops a KIS run without explicit opt-in'
assert_contains "$driver_output" './start.sh --experimental-kis-transition' \
    'start.sh prints the exact KIS opt-in command'
assert_eq '1' "$(wc -l <"$DRIVER_BINARY_LOG" | tr -d ' ')" \
    'start.sh invokes the binary only for detection before refusing the KIS run'

# The Recovery guide is distinct from the DFU-entry guide.
if ! recovery_output="$(guide_recovery_faceid 2>&1)"; then
    fail 'Recovery guide is available for Face ID devices'
fi
assert_contains "$recovery_output" 'Recovery entry instructions' \
    'Recovery guide identifies itself as Recovery entry'
assert_contains "$recovery_output" 'Quickly press and release Volume Up' \
    'Recovery guide starts with Volume Up'
assert_contains "$recovery_output" 'Quickly press and release Volume Down' \
    'Recovery guide continues with Volume Down'
assert_contains "$recovery_output" 'Connect to computer' \
    'Recovery guide explains how to hold Side'
assert_not_contains "$recovery_output" 'DFU entry instructions' \
    'Recovery guide remains separate from DFU entry'

# 7. Validate the repository-owned rule with udev when available.
RULE_FILE="$ROOT_DIR/contrib/udev/99-tr4mpass.rules"
if command -v udevadm >/dev/null 2>&1; then
    udevadm verify "$RULE_FILE" >/dev/null 2>&1 || fail 'udevadm rejects the repository rule'
    pass 'udevadm accepts the repository rule'
else
    printf 'SKIP: udevadm unavailable; syntax validation deferred to host\n'
fi

printf 'wrapper tests: %d passed, 0 failed\n' "$PASS_COUNT"
