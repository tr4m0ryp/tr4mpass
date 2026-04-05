#!/bin/bash
# tr4mpass -- Guided activation lock bypass wrapper
# Cross-platform OS detection, dependency installation, device detection,
# DFU guidance, and bypass execution.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="$SCRIPT_DIR/tr4mpass"

# ------------------------------------------------------------------ #
# ANSI colors                                                         #
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
# Logging helpers                                                     #
# ------------------------------------------------------------------ #

msg_ok()   { printf "${GREEN}[+]${RESET} %s\n" "$1"; }
msg_err()  { printf "${RED}[-]${RESET} %s\n" "$1"; }
msg_info() { printf "${CYAN}[*]${RESET} %s\n" "$1"; }
msg_warn() { printf "${YELLOW}[!]${RESET} %s\n" "$1"; }

# ------------------------------------------------------------------ #
# Banner                                                              #
# ------------------------------------------------------------------ #

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

# ------------------------------------------------------------------ #
# Package manager detection (Linux)                                   #
# ------------------------------------------------------------------ #

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

# pkg-config names for each required library
PKGCONFIG_DEPS=(
    "libimobiledevice-1.0"
    "libirecovery-1.0"
    "libusb-1.0"
    "libplist-2.0"
    "openssl"
)

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

    # Map pkg-config names to brew formula names
    local brew_pkgs="libimobiledevice libirecovery libusb libplist openssl pkg-config"
    msg_info "Installing dependencies via Homebrew..."
    brew install $brew_pkgs
    msg_ok "Homebrew dependencies installed."
}

install_deps_linux_apt() {
    local apt_pkgs="libimobiledevice-dev libirecovery-dev libusb-1.0-0-dev libplist-dev libssl-dev pkg-config build-essential"
    msg_info "Installing dependencies via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y $apt_pkgs
    msg_ok "APT dependencies installed."
}

install_deps_linux_dnf() {
    local dnf_pkgs="libimobiledevice-devel libirecovery-devel libusb1-devel libplist-devel openssl-devel pkg-config gcc make"
    msg_info "Installing dependencies via dnf..."
    sudo dnf install -y $dnf_pkgs
    msg_ok "DNF dependencies installed."
}

install_deps() {
    # Ensure pkg-config itself is available
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
        macos)
            install_deps_macos
            ;;
        linux|wsl)
            detect_linux_pm
            case "$LINUX_PM" in
                apt)    install_deps_linux_apt ;;
                dnf)    install_deps_linux_dnf ;;
                pacman)
                    msg_warn "Arch Linux detected. Install manually:"
                    msg_info "  sudo pacman -S libimobiledevice libirecovery libusb libplist openssl pkg-config base-devel"
                    exit 1
                    ;;
            esac
            ;;
    esac

    # Verify after installation
    check_missing_deps
    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        msg_err "Still missing after install: ${MISSING_DEPS[*]}"
        msg_info "Please install them manually and re-run this script."
        exit 1
    fi

    msg_ok "All dependencies verified."
}

# ------------------------------------------------------------------ #
# Build                                                               #
# ------------------------------------------------------------------ #

build_project() {
    if [ -x "$BINARY" ]; then
        msg_ok "Binary already built: $BINARY"
        return 0
    fi

    msg_info "Building tr4mpass..."
    (cd "$SCRIPT_DIR" && make clean && make)

    if [ ! -x "$BINARY" ]; then
        msg_err "Build failed. Check compiler output above."
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

wait_for_device() {
    local timeout=60
    local elapsed=0
    local interval=2

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

    # Parse fields from tr4mpass --detect-only output
    DEV_MODEL="$(echo "$output" | grep "Product Type:" | sed 's/.*Product Type:[[:space:]]*//')"
    DEV_CHIP_NAME="$(echo "$output" | grep "Chip Name:" | sed 's/.*Chip Name:[[:space:]]*//')"
    DEV_CPID="$(echo "$output" | grep "CPID:" | sed 's/.*CPID:[[:space:]]*//')"
    DEV_IOS="$(echo "$output" | grep "iOS Version:" | sed 's/.*iOS Version:[[:space:]]*//')"
    DEV_SERIAL="$(echo "$output" | grep "Serial:" | sed 's/.*Serial:[[:space:]]*//')"
    DEV_IMEI="$(echo "$output" | grep "IMEI:" | sed 's/.*IMEI:[[:space:]]*//')"
    DEV_CHECKM8="$(echo "$output" | grep "checkm8 vuln:" | sed 's/.*checkm8 vuln:[[:space:]]*//')"
    DEV_DFU="$(echo "$output" | grep "DFU Mode:" | sed 's/.*DFU Mode:[[:space:]]*//')"

    # Determine bypass path and support status
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
    # Face ID devices: iPhone X (10,x), XS/XR (11,x), 11 (12,x), 12+ (13,x+)
    # iPad Pro 3rd gen+ (8,x+), iPad Air 4+ (13,x+)
    local model="$1"
    case "$model" in
        iPhone10,[36])  return 0 ;;  # iPhone X
        iPhone1[1-9],*) return 0 ;;  # iPhone XS and later
        iPad8,*)        return 0 ;;  # iPad Pro 3rd/4th gen (Face ID)
        iPad13,*)       return 0 ;;  # iPad Air 4th+ (no home button)
        iPad14,*)       return 0 ;;  # iPad Pro M2
        *)              return 1 ;;  # Home button device
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

handle_dfu_requirement() {
    # For checkm8 path, device MUST be in DFU mode
    if [ "$DEV_CHECKM8" = "YES" ] && [ "$DEVICE_MODE" != "dfu" ]; then
        msg_warn "checkm8 bypass requires DFU mode, but device is in normal mode."
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
    fi
}

# ------------------------------------------------------------------ #
# Support gating                                                      #
# ------------------------------------------------------------------ #

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
    # (DFU mode devices cannot be queried via lockdownd)
    if [ "$DEVICE_MODE" = "normal" ]; then
        msg_info "Querying device information..."
        parse_device_info
        display_device_info
        gate_support
    else
        # DFU mode -- limited info, but checkm8 path is implied
        echo ""
        echo "========================================"
        echo "  Device Information"
        echo "========================================"
        printf "  %-12s %s\n" "Mode:" "DFU"
        printf "  %-12s %s\n" "Bypass:" "Path A (checkm8)"
        printf "  %-12s %s\n" "Status:" "SUPPORTED (DFU detected)"
        echo "========================================"
        echo ""
        msg_ok "Device in DFU mode. Ready to proceed."
        printf "${BOLD}Press Enter to start bypass...${RESET}"
        read -r
    fi

    echo ""
    msg_info "Starting tr4mpass..."
    echo ""

    exec "$BINARY" "$@"
}

main "$@"
