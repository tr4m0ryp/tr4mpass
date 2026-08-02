#!/bin/bash
# tr4mpass -- Guided activation lock bypass wrapper
# Cross-platform OS detection, dependency installation, device
# detection, DFU guidance, and bypass execution.
#
# The helper routines live in start-helpers.sh so this driver stays
# under the project's 300-line-per-file limit.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/tr4mpass"
HELPERS="$SCRIPT_DIR/start-helpers.sh"

# ------------------------------------------------------------------ #
# ANSI colors (needed by helpers, so initialize before sourcing).    #
# ------------------------------------------------------------------ #

if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    RESET='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' RESET=''
fi

# ------------------------------------------------------------------ #
# pkg-config dependency list.  libssh2 is consumed by the Phase 2C   #
# SSH jailbreak path; if the distro package is missing at build time #
# the production link will fail with "library not found for -lssh2". #
# Install hints are provided by the helpers for every platform.      #
# ------------------------------------------------------------------ #

PKGCONFIG_DEPS=(
    "libimobiledevice-1.0"
    "libirecovery-1.0"
    "libusb-1.0"
    "libplist-2.0"
    "openssl"
    "libcurl"
    "libssh2"
)

# ------------------------------------------------------------------ #
# Source helper functions.                                            #
# ------------------------------------------------------------------ #

if [ ! -f "$HELPERS" ]; then
    printf "[-] Missing %s -- aborting.\n" "$HELPERS" >&2
    exit 1
fi
# shellcheck source=start-helpers.sh
. "$HELPERS"

# ------------------------------------------------------------------ #
# Main                                                                #
# ------------------------------------------------------------------ #

main() {
    print_banner
    detect_os
    install_deps
    build_project

    echo ""
    echo "----------------------------------------"

    if ! wait_for_device; then
        msg_info "Tip: Check your USB cable and try a different port."
        exit 1
    fi

    # Only parse detailed info if device is in normal mode
    # (DFU mode devices cannot be queried via lockdownd).
    if [ "$DEVICE_MODE" = "normal" ]; then
        msg_info "Querying device information..."
        parse_device_info
        display_device_info
        gate_support
    else
        # DFU mode -- query CPID from serial descriptor to determine path
        msg_info "Device in DFU mode. Querying chip info..."
        parse_device_info

        # Derive real support status from parsed CPID
        if [ -z "$DEV_CPID" ] || [ "$DEV_CPID" = "0x0000" ]; then
            msg_warn "Chip ID could not be read from DFU serial descriptor."
            msg_info "This usually means the device is not in true SecureROM DFU mode."
            msg_info "Re-enter DFU mode using the precise button sequence and try again."
            msg_info "  Home button:  Power+Home 10s, release Power, hold Home 5s"
            msg_info "  Face ID:      Vol-Up, Vol-Down, hold Side to black screen,"
            msg_info "                Side+Vol-Down 5s, release Side, hold Vol-Down 10s"
            msg_info "Screen must stay completely BLACK (no Apple logo)."
            exit 1
        fi

        echo ""
        echo "========================================"
        echo "  Device Information"
        echo "========================================"
        printf "  %-12s %s\n" "Mode:"   "DFU"
        printf "  %-12s %s\n" "Chip:"   "${DEV_CHIP_NAME:-(unknown)} (CPID: ${DEV_CPID:-(unknown)})"
        printf "  %-12s %s\n" "Bypass:" "$DEV_BYPASS"
        printf "  %-12s %s\n" "Status:" "$DEV_STATUS"
        echo "========================================"
        echo ""

        if [ "$DEV_STATUS" = "UNSUPPORTED" ]; then
            msg_err "UNSUPPORTED: Device chip could not be identified or is not in the chip database."
            msg_info "This tool supports A5 through A17 chips."
            exit 1
        fi

        msg_ok "Device is supported. Ready to proceed."
        printf "${BOLD}Press Enter to start bypass...${RESET}"
        read -r
    fi

    echo ""
    msg_info "Starting tr4mpass..."
    echo ""

    # Final validation before exec.
    if [ ! -e "$BINARY" ]; then
        msg_err "Binary not found at $BINARY. Build may have failed."
        msg_info "Run 'make' manually to see errors."
        exit 1
    fi
    if [ -d "$BINARY" ]; then
        msg_err "Expected binary at $BINARY but found a directory."
        msg_info "Remove it and rebuild: rm -rf '$BINARY' && make clean && make"
        exit 1
    fi
    if [ ! -x "$BINARY" ]; then
        msg_err "Binary exists but is not executable. Run: chmod +x $BINARY"
        exit 1
    fi

    # On WSL, keep usbipd auto-attaching the device so it stays visible
    # after each USB reset the exploit triggers.
    start_usbipd_auto_attach

    # In DFU mode on some WSL/usbipd stacks, descriptor reads can intermittently
    # fail even when lsusb still reports CPID/ECID.  If parse_device_info()
    # recovered fallback IDs, pass them explicitly unless caller already did.
    local run_args=("$@")
    local has_cpid=0
    local has_ecid=0
    local arg
    for arg in "${run_args[@]}"; do
        case "$arg" in
            --cpid|--cpid=*) has_cpid=1 ;;
            --ecid|--ecid=*) has_ecid=1 ;;
        esac
    done
    if [ "$DEVICE_MODE" = "dfu" ]; then
        if [ $has_cpid -eq 0 ] && [ -n "${DEV_CPID:-}" ] && [ "${DEV_CPID:-}" != "0x0000" ]; then
            run_args+=("--cpid" "$DEV_CPID")
        fi
        if [ $has_ecid -eq 0 ] && [ -n "${DEV_ECID:-}" ] && [ "${DEV_ECID:-}" != "0x0" ]; then
            run_args+=("--ecid" "$DEV_ECID")
        fi
    fi

    # Run binary (not exec so we can clean up usbipd afterwards).
    "$BINARY" "${run_args[@]}"
    _rc=$?
    stop_usbipd_auto_attach
    exit $_rc
}

main "$@"
