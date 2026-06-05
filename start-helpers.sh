start.sh# start-helpers.sh -- Helper functions sourced by start.sh.
#
# This file is not meant to be executed directly.  It houses every
# helper routine (OS detection, dependency install, device detection,
# DFU guidance, support gating) so start.sh can stay under the 300
# line limit required by the project coding rules.
#
# All functions here rely on colour variables (RED, GREEN, ...), the
# BINARY path, and the PKGCONFIG_DEPS array declared by start.sh
# before this file is sourced.

# -----------------------------------------------------------------                                       #
# ------------------------------------------------------------------ #

msg_ok()   { printf "${GREEN}[+]${RESET} %s\n" "$1"; }
msg_err()  { printf "${RED}[-]${RESET} %s\n" "$1"; }
msg_info() { printf "${CYAN}[*]${RESET} %s\n" "$1"; }
msg_warn() { printf "${YELLOW}[!]${RESET} %s\n" "$1"; }

print_banner() {
    printf "${BOLD}"
    echo "========================================"
    echo "  tr4mpass v0.2.0"
    echo "  Activation lock bypass research tool"
    echo "========================================"
    printf "${RESET}\n"
}

# ------------------------------------------------------------------ #
# OS detection                                                        #
# ------------------------------------------------------------------ #

detect_os() {
    local kernel
    kernel="$(uname -s)"

    case "$kernel" in
        Darwin)
            DETECTED_OS="macos"
            msg_ok "Detected OS: macOS ($(sw_vers -productVersion 2>/dev/null || echo 'unknown version'))"
            ;;
        Linux)
            if [ -n "${WSL_DISTRO_NAME:-}" ] || grep -qi microsoft /proc/version 2>/dev/null; then
                DETECTED_OS="wsl"
                msg_ok "Detected OS: Linux (WSL -- ${WSL_DISTRO_NAME:-unknown})"
            else
                DETECTED_OS="linux"
                msg_ok "Detected OS: Linux ($(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || echo 'unknown distro'))"
            fi
            ;;
        MINGW*|MSYS*|CYGWIN*)
            msg_err "Native Windows is not supported."
            msg_info "Please use Windows Subsystem for Linux (WSL) instead."
            msg_info "Install WSL: wsl --install"
            exit 1
            ;;
        *)
            if [ -n "${MSYSTEM:-}" ]; then
                msg_err "Native Windows (MSYS2/Git Bash) is not supported."
                msg_info "Please use Windows Subsystem for Linux (WSL) instead."
                exit 1
            fi
            msg_err "Unsupported platform: $kernel"
            exit 1
            ;;
    esac
}

detect_linux_pm() {
    if command -v apt-get >/dev/null 2>&1; then
        LINUX_PM="apt"
    elif command -v dnf >/dev/null 2>&1; then
        LINUX_PM="dnf"
    elif command -v pacman >/dev/null 2>&1; then
        LINUX_PM="pacman"
    else
        msg_err "No supported package manager found (need apt, dnf, or pacman)."
        exit 1
    fi
}

# ------------------------------------------------------------------ #
# Dependency installation                                             #
# ------------------------------------------------------------------ #

check_missing_deps() {
    MISSING_DEPS=()
    for dep in "${PKGCONFIG_DEPS[@]}"; do
        if ! pkg-config --exists "$dep" 2>/dev/null; then
            MISSING_DEPS+=("$dep")
        fi
    done
}

install_deps_macos() {
    if ! command -v brew >/dev/null 2>&1; then
        msg_err "Homebrew is not installed."
        msg_info "Install it: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi

    local brew_pkgs="libimobiledevice libirecovery libusb libplist openssl pkg-config libssh2"
    msg_info "Installing dependencies via Homebrew..."
    brew install $brew_pkgs
    msg_ok "Homebrew dependencies installed."
}

install_deps_linux_apt() {
    local apt_pkgs="libimobiledevice-dev libirecovery-1.0-dev libusb-1.0-0-dev libplist-dev libssl-dev libssh2-1-dev pkg-config build-essential"
    msg_info "Installing dependencies via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y $apt_pkgs
    msg_ok "APT dependencies installed."
}

install_deps_linux_dnf() {
    local dnf_pkgs="libimobiledevice-devel libirecovery-devel libusb1-devel libplist-devel openssl-devel libssh2-devel pkg-config gcc make"
    msg_info "Installing dependencies via dnf..."
    sudo dnf install -y $dnf_pkgs
    msg_ok "DNF dependencies installed."
}

install_deps() {
    if ! command -v pkg-config >/dev/null 2>&1; then
        msg_warn "pkg-config not found, installing it first..."
        case "$DETECTED_OS" in
            macos) brew install pkg-config ;;
            linux|wsl)
                detect_linux_pm
                case "$LINUX_PM" in
                    apt) sudo apt-get update -qq && sudo apt-get install -y pkg-config ;;
                    dnf) sudo dnf install -y pkg-config ;;
                    pacman) sudo pacman -S --noconfirm pkg-config ;;
                esac
                ;;
        esac
    fi

    check_missing_deps

    if [ ${#MISSING_DEPS[@]} -eq 0 ]; then
        msg_ok "All dependencies are already installed."
        return 0
    fi

    msg_warn "Missing dependencies: ${MISSING_DEPS[*]}"

    case "$DETECTED_OS" in
        macos) install_deps_macos ;;
        linux|wsl)
            detect_linux_pm
            case "$LINUX_PM" in
                apt) install_deps_linux_apt ;;
                dnf) install_deps_linux_dnf ;;
                pacman)
                    msg_warn "Arch Linux detected. Install from AUR:"
                    msg_info "  yay -S libimobiledevice libirecovery libusb libplist openssl libssh2 pkg-config base-devel"
                    msg_info "  or: paru -S libimobiledevice-git libirecovery-git libssh2"
                    msg_info "Re-run this script after installing."
                    exit 1
                    ;;
            esac
            ;;
    esac

    check_missing_deps
    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        msg_err "Still missing after install: ${MISSING_DEPS[*]}"
        msg_info "Please install them manually and re-run this script."
        msg_info "  libssh2 is required for Phase 2C SSH jailbreak delivery."
        msg_info "  If libssh2 was just installed, re-running should pick it up."
        exit 1
    fi

    msg_ok "All dependencies verified."
}

# ------------------------------------------------------------------ #
# Build                                                               #
# ------------------------------------------------------------------ #

build_project() {
    if [ -f "$BINARY" ] && [ -x "$BINARY" ]; then
        msg_ok "Binary already built: $BINARY"
        return 0
    fi

    if [ -d "$BINARY" ]; then
        msg_err "Expected binary at $BINARY but found a directory."
        msg_info "Remove it and rebuild: rm -rf '$BINARY' && make clean && make"
        exit 1
    fi

    msg_info "Building tr4mpass..."
    if ! (cd "$SCRIPT_DIR" && make clean && make); then
        msg_err "Build failed. Check compiler output above."
        msg_info "If the linker reports 'library not found for -lssh2', install libssh2"
        msg_info "  macOS:  brew install libssh2"
        msg_info "  Debian: sudo apt install libssh2-1-dev"
        msg_info "  Fedora: sudo dnf install libssh2-devel"
        msg_info "  Arch:   sudo pacman -S libssh2"
        exit 1
    fi

    if [ ! -f "$BINARY" ]; then
        msg_err "Build completed but binary not found at $BINARY."
        exit 1
    fi

    if [ ! -x "$BINARY" ]; then
        msg_err "Binary exists but is not executable. Run: chmod +x $BINARY"
        exit 1
    fi

    msg_ok "Build successful."
}

# ------------------------------------------------------------------ #
# Device detection                                                    #
# ------------------------------------------------------------------ #

check_dfu() {
    case "$DETECTED_OS" in
        macos)
            system_profiler SPUSBDataType 2>/dev/null | grep -q "0x1227"
            return $?
            ;;
        linux|wsl)
            if command -v lsusb >/dev/null 2>&1; then
                lsusb 2>/dev/null | grep -q "05ac:1227"
                return $?
            fi
            return 1
            ;;
    esac
    return 1
}

check_normal() {
    if command -v idevice_id >/dev/null 2>&1; then
        local devices
        devices="$(idevice_id -l 2>/dev/null || true)"
        if [ -n "$devices" ]; then
            return 0
        fi
    fi
    return 1
}

check_wsl_usb_passthrough() {
    msg_warn "WSL detected: USB devices require passthrough via usbipd-win."
    echo ""
    echo "  To pass through your iOS device:"
    echo "  1. In Windows PowerShell (Admin):"
    echo "       usbipd list                     # find your device bus ID"
    echo "       usbipd bind --busid <id>"
    echo "       usbipd attach --busid <id> --wsl"
    echo "  2. Install usbipd if not present:"
    echo "       winget install usbipd"
    echo "  3. Re-run ./start.sh after attaching."
    echo ""
    if lsusb 2>/dev/null | grep -q "05ac"; then
        msg_ok "Apple device visible via lsusb -- USB passthrough is working."
    else
        msg_err "No Apple USB device visible in WSL. Complete the passthrough steps above."
    fi
}

wait_for_device() {
    local timeout=60
    local elapsed=0
    local interval=2
    local wsl_warned=0

    msg_info "Connect your iOS device via USB cable."
    echo ""

    while [ $elapsed -lt $timeout ]; do
        if check_dfu; then
            DEVICE_MODE="dfu"
            msg_ok "Device detected in DFU mode!"
            return 0
        fi
        if check_normal; then
            DEVICE_MODE="normal"
            msg_ok "Device detected in normal mode!"
            return 0
        fi
        if [ "$DETECTED_OS" = "wsl" ] && [ "$wsl_warned" -eq 0 ] && [ "$elapsed" -ge "$interval" ]; then
            echo ""
            check_wsl_usb_passthrough
            echo ""
            wsl_warned=1
        fi
        printf "\r${CYAN}[*]${RESET} Waiting for device... %ds / %ds" "$elapsed" "$timeout"
        sleep "$interval"
        elapsed=$((elapsed + interval))
    done

    echo ""
    msg_err "No device detected within ${timeout}s timeout."
    return 1
}

# ------------------------------------------------------------------ #
# Device info parsing                                                 #
# ------------------------------------------------------------------ #

parse_device_info() {
    local output
    output="$("$BINARY" --detect-only 2>&1)" || true

    if [ -z "$output" ]; then
        msg_err "Device query returned no output (binary may have crashed or device disconnected)."
        msg_info "Ensure the device is still connected and try again."
        msg_info "On Linux: check that usbmuxd is running: sudo systemctl status usbmuxd"
        DEV_MODEL="" DEV_CHIP_NAME="" DEV_CPID="" DEV_IOS=""
        DEV_SERIAL="" DEV_IMEI="" DEV_CHECKM8="" DEV_DFU=""
        DEV_BYPASS="(none)"
        DEV_STATUS="UNSUPPORTED"
        return 0
    fi

    DEV_MODEL="$(echo "$output" | grep "Product Type:" | sed 's/.*Product Type:[[:space:]]*//')"
    DEV_CHIP_NAME="$(echo "$output" | grep "Chip Name:" | sed 's/.*Chip Name:[[:space:]]*//')"
    DEV_CPID="$(echo "$output" | grep "CPID:" | sed 's/.*CPID:[[:space:]]*//')"
    DEV_IOS="$(echo "$output" | grep "iOS Version:" | sed 's/.*iOS Version:[[:space:]]*//')"
    DEV_SERIAL="$(echo "$output" | grep "Serial:" | sed 's/.*Serial:[[:space:]]*//')"
    DEV_IMEI="$(echo "$output" | grep "IMEI:" | sed 's/.*IMEI:[[:space:]]*//')"
    DEV_CHECKM8="$(echo "$output" | grep "checkm8 vuln:" | sed 's/.*checkm8 vuln:[[:space:]]*//')"
    DEV_DFU="$(echo "$output" | grep "DFU Mode:" | sed 's/.*DFU Mode:[[:space:]]*//')"

    if [ "$DEV_CHECKM8" = "YES" ]; then
        DEV_BYPASS="Path A (checkm8, A5-A11)"
        DEV_STATUS="SUPPORTED"
    elif [ -n "$DEV_CPID" ] && [ "$DEV_CPID" != "0x0000" ]; then
        DEV_BYPASS="Path B (identity, A12+)"
        DEV_STATUS="SUPPORTED"
    else
        DEV_BYPASS="(none)"
        DEV_STATUS="UNSUPPORTED"
    fi
    return 0
}

display_device_info() {
    echo ""
    echo "========================================"
    echo "  Device Information"
    echo "========================================"
    printf "  %-12s %s\n" "Model:"   "${DEV_MODEL:-(unknown)}"
    printf "  %-12s %s (CPID: %s)\n" "Chip:" "${DEV_CHIP_NAME:-(unknown)}" "${DEV_CPID:-(unknown)}"
    printf "  %-12s %s\n" "iOS:"     "${DEV_IOS:-(unknown)}"
    printf "  %-12s %s\n" "Serial:"  "${DEV_SERIAL:-(unknown)}"
    printf "  %-12s %s\n" "IMEI:"    "${DEV_IMEI:-(none / WiFi-only)}"
    printf "  %-12s %s\n" "Mode:"    "${DEVICE_MODE:-unknown}"
    printf "  %-12s %s\n" "Bypass:"  "$DEV_BYPASS"
    printf "  %-12s %s\n" "Status:"  "$DEV_STATUS"
    echo "========================================"
    echo ""
}

# ------------------------------------------------------------------ #
# DFU mode guide                                                      #
# ------------------------------------------------------------------ #

is_faceid_device() {
    local model="$1"
    case "$model" in
        iPhone10,[36])  return 0 ;;
        iPhone1[1-9],*) return 0 ;;
        iPad8,*)        return 0 ;;
        iPad13,*)       return 0 ;;
        iPad14,*)       return 0 ;;
        *)              return 1 ;;
    esac
}

guide_dfu_home_button() {
    echo ""
    msg_info "DFU entry instructions (Home button device):"
    echo ""
    echo "  1. Connect device to computer via USB"
    echo "  2. Power off the device completely"
    echo "  3. Hold Power + Home for 10 seconds"
    echo "  4. Release Power, keep holding Home for 5 more seconds"
    echo "  5. Screen must be BLACK (not Apple logo)"
    echo ""
    echo "  If you see the Apple logo, you entered recovery mode."
    echo "  Start over from step 2."
    echo ""
}

guide_dfu_faceid() {
    echo ""
    msg_info "DFU entry instructions (Face ID / no Home button device):"
    echo ""
    echo "  1. Connect device to computer via USB"
    echo "  2. Quick-press Volume Up, then Volume Down"
    echo "  3. Hold Side button until screen goes black"
    echo "  4. Hold Side + Volume Down for 5 seconds"
    echo "  5. Release Side, keep holding Volume Down for 10 seconds"
    echo "  6. Screen must be BLACK (not Apple logo)"
    echo ""
    echo "  If you see the Apple logo, you entered recovery mode."
    echo "  Start over from step 2."
    echo ""
}

wait_for_dfu_entry() {
    local reason="$1"
    msg_warn "$reason"
    echo ""

    if is_faceid_device "${DEV_MODEL:-}"; then
        guide_dfu_faceid
    else
        guide_dfu_home_button
    fi

    msg_info "Waiting for DFU mode..."

    local timeout=120
    local elapsed=0
    local interval=2

    while [ $elapsed -lt $timeout ]; do
        if check_dfu; then
            DEVICE_MODE="dfu"
            msg_ok "Device detected in DFU mode!"
            return 0
        fi
        printf "\r${CYAN}[*]${RESET} Waiting for DFU... %ds / %ds" "$elapsed" "$timeout"
        sleep "$interval"
        elapsed=$((elapsed + interval))
    done

    echo ""
    msg_err "DFU mode not detected within ${timeout}s."
    msg_info "Please retry DFU entry and run this script again."
    exit 1
}

handle_dfu_requirement() {
    if [ "$DEVICE_MODE" = "dfu" ]; then
        return 0
    fi
    if [ "$DEV_CHECKM8" = "YES" ]; then
        wait_for_dfu_entry "Path A (checkm8) requires DFU mode, but device is in normal mode."
    elif [ "$DEV_STATUS" = "SUPPORTED" ]; then
        wait_for_dfu_entry "Path B (A12+ bypass) requires DFU mode, but device is in normal mode."
    fi
}

gate_support() {
    if [ "$DEV_STATUS" = "UNSUPPORTED" ]; then
        msg_err "UNSUPPORTED: Device chip could not be identified or is not in the chip database."
        msg_info "Ensure the device is properly connected and recognized."
        msg_info "This tool supports A5 through A17 chips."
        exit 1
    fi

    handle_dfu_requirement

    msg_ok "Device is supported. Ready to proceed."
    echo ""
    printf "${BOLD}Press Enter to start bypass...${RESET}"
    read -r
}
