#!/bin/bash
# start-helpers.sh -- Helper functions sourced by start.sh.
#
# This file is not meant to be executed directly.  It houses every
# helper routine (OS detection, dependency install, device detection,
# DFU guidance, support gating) so start.sh can stay under the 300
# line limit required by the project coding rules.
#
# All functions here rely on colour variables (RED, GREEN, ...), the
# BINARY path, and the PKGCONFIG_DEPS array declared by start.sh
# before this file is sourced.

# ------------------------------------------------------------------ #
# Logging helpers                                                     #
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
    elif command -v zypper >/dev/null 2>&1; then
        LINUX_PM="zypper"
    elif command -v apk >/dev/null 2>&1; then
        LINUX_PM="apk"
    else
        msg_err "No supported package manager found (need apt, dnf, pacman, zypper, or apk)."
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

    local brew_pkgs="libimobiledevice libirecovery libusb libplist openssl curl pkg-config libssh2"
    msg_info "Installing dependencies via Homebrew..."
    brew install $brew_pkgs
    msg_ok "Homebrew dependencies installed."
}

# openssl and curl are keg-only under Homebrew; their .pc files never
# land on the default pkg-config search path. Prepend the two prefixes
# so pkg-config can find libcrypto and libcurl without editing the
# Makefile.
macos_prep_pkgconfig() {
    if ! command -v brew >/dev/null 2>&1; then
        return 0
    fi
    local ossl_prefix curl_prefix extra=""
    ossl_prefix="$(brew --prefix openssl 2>/dev/null || true)"
    curl_prefix="$(brew --prefix curl 2>/dev/null || true)"
    [ -n "$ossl_prefix" ] && extra="${ossl_prefix}/lib/pkgconfig"
    [ -n "$curl_prefix" ] && extra="${extra:+${extra}:}${curl_prefix}/lib/pkgconfig"
    if [ -n "$extra" ]; then
        export PKG_CONFIG_PATH="${extra}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    fi
}

install_deps_linux_apt() {
    local irecovery_pkg curl_dev_pkg apt_pkgs

    if apt-cache show libirecovery-1.0-dev >/dev/null 2>&1; then
        irecovery_pkg="libirecovery-1.0-dev"
    elif apt-cache show libirecovery-dev >/dev/null 2>&1; then
        irecovery_pkg="libirecovery-dev"
    else
        irecovery_pkg="libirecovery-1.0-dev"
    fi

    if apt-cache show libcurl4-openssl-dev >/dev/null 2>&1; then
        curl_dev_pkg="libcurl4-openssl-dev"
    elif apt-cache show libcurl4-gnutls-dev >/dev/null 2>&1; then
        curl_dev_pkg="libcurl4-gnutls-dev"
    else
        curl_dev_pkg="libcurl4-openssl-dev"
    fi

    apt_pkgs="libimobiledevice-dev $irecovery_pkg libusb-1.0-0-dev libplist-dev libssl-dev $curl_dev_pkg libssh2-1-dev pkg-config build-essential usbutils usbmuxd"
    msg_info "Installing dependencies via apt..."
    sudo apt-get update -qq
    sudo apt-get install -y $apt_pkgs
    msg_ok "APT dependencies installed."
}

install_deps_linux_dnf() {
    local dnf_pkgs="libimobiledevice-devel libirecovery-devel libusb1-devel libplist-devel openssl-devel libcurl-devel libssh2-devel pkg-config gcc make"
    msg_info "Installing dependencies via dnf..."
    sudo dnf install -y $dnf_pkgs
    msg_ok "DNF dependencies installed."
}

install_deps_linux_pacman() {
    local pacman_pkgs="libimobiledevice libirecovery libusb libplist openssl libssh2 curl pkgconf base-devel"
    msg_info "Installing dependencies via pacman..."
    sudo pacman -S --needed --noconfirm $pacman_pkgs
    msg_ok "pacman dependencies installed."
}

install_deps_linux_zypper() {
    local zypper_pkgs="libimobiledevice-devel libirecovery-devel libusb-1_0-devel libplist-2_0-devel openssl-devel libssh2-devel libcurl-devel pkg-config gcc make"
    msg_info "Installing dependencies via zypper..."
    sudo zypper install -y $zypper_pkgs
    msg_ok "zypper dependencies installed."
}

install_deps_linux_apk() {
    local apk_pkgs="libimobiledevice-dev libirecovery-dev libusb-dev libplist-dev openssl-dev libssh2-dev curl-dev pkgconfig gcc make musl-dev"
    msg_info "Installing dependencies via apk..."
    sudo apk add --no-cache $apk_pkgs
    msg_ok "apk dependencies installed."
}

install_deps() {
    if [ "$DETECTED_OS" = "macos" ]; then
        macos_prep_pkgconfig
    fi

    if ! command -v pkg-config >/dev/null 2>&1; then
        msg_warn "pkg-config not found, installing it first..."
        case "$DETECTED_OS" in
            macos) brew install pkg-config ;;
            linux|wsl)
                detect_linux_pm
                case "$LINUX_PM" in
                    apt)    sudo apt-get update -qq && sudo apt-get install -y pkg-config ;;
                    dnf)    sudo dnf install -y pkg-config ;;
                    pacman) sudo pacman -S --needed --noconfirm pkgconf ;;
                    zypper) sudo zypper install -y pkg-config ;;
                    apk)    sudo apk add --no-cache pkgconfig ;;
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
            macos_prep_pkgconfig
            ;;
        linux|wsl)
            detect_linux_pm
            case "$LINUX_PM" in
                apt)    install_deps_linux_apt ;;
                dnf)    install_deps_linux_dnf ;;
                pacman) install_deps_linux_pacman ;;
                zypper) install_deps_linux_zypper ;;
                apk)    install_deps_linux_apk ;;
            esac
            ;;
    esac

    check_missing_deps
    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        msg_err "Still missing after install: ${MISSING_DEPS[*]}"
        msg_info "Please install them manually and re-run this script."
        msg_info "  libssh2 is required for Phase 2C SSH jailbreak delivery."
        msg_info "  libcurl is required for Albert activation server calls."
        msg_info "  If either was just installed, re-running should pick it up."
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

start_usbipd_auto_attach() {
    # On WSL, launch usbipd auto-attach in the background so the device
    # is automatically re-attached after each USB reset during the exploit.
    # This is critical for checkm8: the exploit intentionally resets the
    # USB bus, and usbipd drops the attachment on every disconnect.
    #
    # Uses --hardware-id 05ac:1227 (Apple DFU VID:PID) so we don't need
    # to know the bus ID.  Runs via powershell.exe (Windows-side) with
    # the process in a hidden window so there's no visible popup.
    #
    # Returns the Windows PID in USBIPD_AUTOATTACH_PID, or empty string
    # on failure (non-fatal -- exploit may still succeed on first try).
    USBIPD_AUTOATTACH_PID=""
    if [ "$DETECTED_OS" != "wsl" ]; then
        return 0
    fi
    if ! command -v powershell.exe >/dev/null 2>&1; then
        return 0
    fi
    msg_info "Starting usbipd auto-attach (keeps device visible after USB resets)..."
    USBIPD_AUTOATTACH_PID=$(
        powershell.exe -NoProfile -Command \
            'Start-Process -FilePath usbipd -ArgumentList "attach","--hardware-id","05ac:1227","--wsl","--auto-attach" -WindowStyle Hidden -PassThru | Select-Object -ExpandProperty Id' \
            2>/dev/null | tr -d '\r\n'
    ) || USBIPD_AUTOATTACH_PID=""
    if [ -n "$USBIPD_AUTOATTACH_PID" ]; then
        msg_ok "usbipd auto-attach running (PID $USBIPD_AUTOATTACH_PID)."
    else
        msg_warn "Could not start usbipd auto-attach; exploit retries may fail if device resets."
    fi
}

stop_usbipd_auto_attach() {
    if [ -z "${USBIPD_AUTOATTACH_PID:-}" ]; then
        return 0
    fi
    powershell.exe -NoProfile -Command \
        "Stop-Process -Id $USBIPD_AUTOATTACH_PID -Force -ErrorAction SilentlyContinue" \
        2>/dev/null || true
    USBIPD_AUTOATTACH_PID=""
}

ensure_usbmuxd() {
    # usbmuxd is required for idevice_id to see normal-mode devices.
    # If it's not running, attempt a non-interactive start (never block
    # for a sudo password -- if it fails we fall back gracefully).
    if ! command -v usbmuxd >/dev/null 2>&1; then
        return 0  # not installed; check_normal will fail gracefully
    fi
    if ! pgrep -x usbmuxd >/dev/null 2>&1; then
        msg_info "usbmuxd not running, attempting to start it..."
        # -n = non-interactive: fail immediately instead of prompting
        sudo -n usbmuxd 2>/dev/null || usbmuxd 2>/dev/null || true
        sleep 1
        if pgrep -x usbmuxd >/dev/null 2>&1; then
            msg_ok "usbmuxd started."
        else
            msg_warn "Could not start usbmuxd automatically."
            msg_info "Normal-mode detection may not work. Try: sudo usbmuxd"
        fi
    fi
}

wait_for_device() {
    local timeout=60
    local elapsed=0
    local interval=2

    msg_info "Connect your iOS device via USB cable."
    echo ""

    # Ensure usbmuxd is running so idevice_id can see normal-mode devices.
    if [ "$DETECTED_OS" = "linux" ] || [ "$DETECTED_OS" = "wsl" ]; then
        ensure_usbmuxd
    fi

    # On WSL show the USB passthrough instructions up front so the user
    # sees them immediately, rather than discovering them mid-poll.
    if [ "$DETECTED_OS" = "wsl" ]; then
        check_wsl_usb_passthrough
        echo ""
    fi

    while [ $elapsed -lt $timeout ]; do
        if check_dfu; then
            DEVICE_MODE="dfu"
            echo ""
            msg_ok "Device detected in DFU mode!"
            return 0
        fi
        if check_normal; then
            DEVICE_MODE="normal"
            echo ""
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
    local dfu_serial_fallback

    DEV_MODEL="" DEV_CHIP_NAME="" DEV_CPID="" DEV_ECID="" DEV_IOS=""
    DEV_SERIAL="" DEV_IMEI="" DEV_CHECKM8="" DEV_DFU=""
    DEV_BYPASS="(none)"
    DEV_STATUS="UNSUPPORTED"

    # On Linux/WSL DFU, prefer lsusb directly so the wrapper does not
    # consume a libusb session before the real exploit starts.
    if [ "$DEVICE_MODE" = "dfu" ] && command -v lsusb >/dev/null 2>&1; then
        dfu_serial_fallback="$(
            lsusb -v -d 05ac:1227 2>/dev/null |
            sed -n 's/.*iSerial[[:space:]]\+[0-9]\+[[:space:]]\+//p' |
            grep 'CPID:' | head -n1
        )"
        if [ -n "$dfu_serial_fallback" ]; then
            DEV_SERIAL="$dfu_serial_fallback"
            DEV_CPID="$(printf '%s\n' "$dfu_serial_fallback" | sed -n 's/.*CPID:\([0-9A-Fa-f]\+\).*/0x\1/p')"
            DEV_ECID="$(printf '%s\n' "$dfu_serial_fallback" | sed -n 's/.*ECID:\([0-9A-Fa-f]\+\).*/0x\1/p')"
            DEV_DFU="YES"
            msg_warn "Using DFU serial info from lsusb to avoid a pre-exploit libusb probe."
        fi
    fi

    if [ "$DEVICE_MODE" = "dfu" ] && [ -n "$DEV_CPID" ] && [ "$DEV_CPID" != "0x0000" ]; then
        case "${DEV_CPID#0x}" in
            8950|8955|8947|7002|8002|8960|7000|7001|8000|8003|8001|8010|8011|8012|8015)
                DEV_CHECKM8="YES"
                DEV_BYPASS="Path A (checkm8, A5-A11)"
                DEV_STATUS="SUPPORTED"
                ;;
            *)
                DEV_CHECKM8="NO"
                DEV_BYPASS="Path B (identity, A12+)"
                DEV_STATUS="SUPPORTED"
                ;;
        esac
        return 0
    fi

    output="$("$BINARY" --detect-only 2>&1)" || true

    if [ -z "$output" ]; then
        msg_err "Device query returned no output (binary may have crashed or device disconnected)."
        msg_info "Ensure the device is still connected and try again."
        msg_info "On Linux: check that usbmuxd is running: sudo systemctl status usbmuxd"
        return 0
    fi

    DEV_MODEL="$(echo "$output" | grep "Product Type:" | sed 's/.*Product Type:[[:space:]]*//')"
    DEV_CHIP_NAME="$(echo "$output" | grep "Chip Name:" | sed 's/.*Chip Name:[[:space:]]*//')"
    DEV_CPID="$(echo "$output" | grep "CPID:" | sed 's/.*CPID:[[:space:]]*//')"
    DEV_ECID="$(echo "$output" | grep "ECID:" | sed 's/.*ECID:[[:space:]]*//')"
    DEV_IOS="$(echo "$output" | grep "iOS Version:" | sed 's/.*iOS Version:[[:space:]]*//')"
    DEV_SERIAL="$(echo "$output" | grep "Serial:" | sed 's/.*Serial:[[:space:]]*//')"
    DEV_IMEI="$(echo "$output" | grep "IMEI:" | sed 's/.*IMEI:[[:space:]]*//')"
    DEV_CHECKM8="$(echo "$output" | grep "checkm8 vuln:" | sed 's/.*checkm8 vuln:[[:space:]]*//')"
    DEV_DFU="$(echo "$output" | grep "DFU Mode:" | sed 's/.*DFU Mode:[[:space:]]*//')"

    # Fallback: if libusb string descriptor reads time out, parse CPID/ECID
    # from lsusb's iSerial line so the exploit can proceed with --cpid/--ecid.
    if [ "$DEVICE_MODE" = "dfu" ] &&
       { [ -z "$DEV_CPID" ] || [ "$DEV_CPID" = "0x0000" ]; } &&
       command -v lsusb >/dev/null 2>&1; then
        dfu_serial_fallback="$(
            lsusb -v -d 05ac:1227 2>/dev/null |
            sed -n 's/.*iSerial[[:space:]]\+[0-9]\+[[:space:]]\+//p' |
            grep 'CPID:' | head -n1
        )"
        if [ -n "$dfu_serial_fallback" ]; then
            DEV_SERIAL="$dfu_serial_fallback"
            DEV_CPID="$(printf '%s\n' "$dfu_serial_fallback" | sed -n 's/.*CPID:\([0-9A-Fa-f]\+\).*/0x\1/p')"
            DEV_ECID="$(printf '%s\n' "$dfu_serial_fallback" | sed -n 's/.*ECID:\([0-9A-Fa-f]\+\).*/0x\1/p')"
            msg_warn "Using DFU serial fallback from lsusb (libusb descriptor reads timed out)."
        fi
    fi

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
