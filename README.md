# tr4mpass: iOS DFU Research Toolkit

<p align="center">
  <a href="https://x.com/tr4m0ryp"><img alt="Follow on X" src="https://img.shields.io/badge/X-@tr4m0ryp-000000?logo=x&logoColor=white"></a>
</p>

# New improved version will be released soon
Open-source toolkit for iOS DFU-mode research and device provisioning. Runs locally on your computer -- no servers, no payments, no accounts needed.

**Supports iOS 12 through 26.1 | iPhone 5s through iPhone 15 Pro**

---

## How to Use

### 1. Download

```bash
git clone https://github.com/tr4m0ryp/tr4mpass.git
cd tr4mpass
```

### 2. Run

```bash
./start.sh
```

That's it. The script will:
- Install everything needed (works on macOS, Linux, and Windows via WSL)
- Build the tool automatically
- Detect your device when you plug it in
- Guide you into DFU mode step by step
- Run the process
- Tell you when it's done

> **Best results for A5-A11 devices:** use native Linux (including Kali) or macOS with direct USB access. WSL + usbipd may have timing issues.

### 3. Put Your Device in DFU Mode

The script will walk you through this, but here's the short version:

**iPhone 7 and older (Home button):**
1. Connect to your computer via USB
2. Hold Power + Home for 10 seconds
3. Release Power, keep holding Home for 5 more seconds
4. Screen should be completely black (no Apple logo)

**iPhone 8, X, and newer (Face ID / no Home button):**
1. Connect to your computer via USB
2. Quick-press Volume Up, quick-press Volume Down
3. Hold Side button until screen goes black
4. Hold Side + Volume Down for 5 seconds
5. Release Side, keep holding Volume Down for 10 seconds
6. Screen should be completely black

---

## Supported Devices

### iPhone

| Device | Chip | Method | Signal Preserved |
|--------|------|--------|------------------|
| iPhone 5s | A7 | Path A | No (WiFi only) |
| iPhone 6 / 6 Plus | A8 | Path A | No (WiFi only) |
| iPhone 6s / 6s Plus | A9 | Path A | Yes |
| iPhone SE (1st gen) | A9 | Path A | Yes |
| iPhone 7 / 7 Plus | A10 | Path A | Yes |
| iPhone 8 / 8 Plus | A11 | Path A | Yes |
| iPhone X | A11 | Path A | Yes |
| iPhone XR | A12 | Path B | Yes |
| iPhone XS / XS Max | A12 | Path B | Yes |
| iPhone 11 / Pro / Pro Max | A13 | Path B | Yes |
| iPhone SE (2nd gen) | A13 | Path B | Yes |
| iPhone 12 / Mini / Pro / Pro Max | A14 | Path B | Yes |
| iPhone 13 / Mini / Pro / Pro Max | A15 | Path B | Yes |
| iPhone SE (3rd gen) | A15 | Path B | Yes |
| iPhone 14 / Plus | A15 | Path B | Yes |
| iPhone 14 Pro / Pro Max | A16 | Path B | Yes |
| iPhone 15 / Plus | A16 | Path B | Yes |
| iPhone 15 Pro / Pro Max | A17 | Path B | Yes |

### iPad

| Device | Chip | Method |
|--------|------|--------|
| iPad Air | A7 | Path A |
| iPad Air 2 | A8X | Path A |
| iPad mini 4 | A8 | Path A |
| iPad Pro (1st gen) | A9X | Path A |
| iPad (5th-7th gen) | A9-A10 | Path A |
| iPad Pro (2nd gen) | A10X | Path A |
| iPad Pro (3rd/4th gen) | A12/A12Z | Path B |
| iPad Air (4th/5th gen) | A14/M1 | Path B |
| iPad Pro (5th/6th gen) | M1/M2 | Path B |

### Other

| Device | Chip | Method |
|--------|------|--------|
| iPod touch (6th gen) | A8 | Path A |
| iPod touch (7th gen) | A10 | Path A |
| Apple TV 4K (1st gen) | A10X | Path A |
| Apple Watch Series 1-3 | S1P/S3 | Path A |

---

## What Does "Signal Preserved" Mean?

- **Yes** -- After the process, your iPhone keeps full cellular service (calls, texts, data). SIM card works normally.
- **No (WiFi only)** -- After the process, the device works on WiFi but cellular service is disabled. This applies to older devices (A7/A8) and all WiFi-only iPads.

---

## Advanced Usage

If you prefer to run things manually instead of using `start.sh`:

```bash
# Build
make

# Just identify your device (no process)
./tr4mpass --detect-only

# Run with full debug output
./tr4mpass --verbose

# Force a specific path
./tr4mpass --force-path-a    # A5-A11 devices
./tr4mpass --force-path-b    # A12+ devices

# Preview what would happen without doing it
./tr4mpass --dry-run
```

<details>
<summary>Manual dependency install -- macOS</summary>

```bash
brew install libimobiledevice libirecovery libusb libplist openssl pkg-config
```

</details>

<details>
<summary>Manual dependency install -- Linux (Debian/Ubuntu)</summary>

```bash
sudo apt-get install -y \
    libimobiledevice-dev libusb-1.0-0-dev libplist-dev \
    libssl-dev libssh2-1-dev pkg-config build-essential usbutils usbmuxd

# Debian/Kali package names vary by release for libirecovery and libcurl dev:
sudo apt-get install -y libirecovery-1.0-dev || sudo apt-get install -y libirecovery-dev
sudo apt-get install -y libcurl4-openssl-dev || sudo apt-get install -y libcurl4-gnutls-dev
```

</details>

<details>
<summary>Manual dependency install -- Linux (Fedora)</summary>

```bash
sudo dnf install -y \
    libimobiledevice-devel libirecovery-devel libusb1-devel \
    libplist-devel openssl-devel pkg-config gcc make
```

</details>

<details>
<summary>Windows</summary>

Native Windows is not supported. Install WSL first:

```powershell
wsl --install
```

Then open a WSL terminal and follow the Linux instructions.

</details>

---

## Architecture

```
+-------------------------+
|        start.sh         |  Guided wrapper (OS detect, deps, DFU guide)
+-------------------------+
|         main.c          |  CLI, device detection, path selection
+-------------------------+
|  Path A    |   Path B   |  Processing strategies
+------------+------------+
| DFU comm   | identity   |  Device communication
| ramdisk    | session    |  Activation protocol
+------------+------------+
| DFU protocol | activation record | deletescript |
+--------------+-------------------+--------------+
| libusb | libimobiledevice | libplist | OpenSSL  |
+--------+------------------+---------+----------+
```

### Source Files

The codebase is written in C99 (21 source files, 20 headers). Every file is under 300 lines. The Makefile auto-discovers all sources -- no manual file lists.

| Module | What it does |
|--------|-------------|
| `src/main.c` | Entry point, CLI args, orchestration |
| `src/device/` | Device detection (USB + libimobiledevice), chip database (A5-A17) |
| `src/dfu/` | DFU communication stages, protocol handling, data assembly |
| `src/pipeline/` | Path A, Path B, signal detection, deletescript, AFC utils |
| `src/activation/` | Activation client, session protocol, record builder |
| `src/util/` | Logging, plist helpers, USB helpers |

---

## Disclaimer

This tool is for **authorized use only**.

- Only use on devices you own or have written authorization to test.
- No warranty. Use at your own risk.
- Results are not guaranteed for every device or iOS version.

---

## References

### Open-Source Tools and Libraries

| Project | What we used it for |
|---------|-------------------|
| [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice) | Device communication, lockdownd, mobileactivation API |
| [libideviceactivation](https://github.com/libimobiledevice/libideviceactivation) | Activation record handling, drmHandshake protocol |
| [go-ios](https://github.com/danielpaulus/go-ios) | mobileactivation module -- session protocol, handshake sequence, activation flow |
| [pymobiledevice3](https://github.com/doronz88/pymobiledevice3) | Python reference for activation protocol and mobile services |
| [img4tool](https://github.com/Tihmstar/img4tool) | IMG4 firmware container extraction |
| [iBoot64Patcher](https://github.com/Tihmstar/iBoot64Patcher) | Bootloader patching for ramdisk loading |
| [ramdiskutil](https://github.com/cfw-project/ramdiskutil) | iOS restore ramdisk customization |
| [iExtractor](https://github.com/malus-security/iextractor) | Automated IPSW firmware extraction |

### Apple Documentation

| Source | What it covers |
|--------|---------------|
| [Apple Device Activation](https://support.apple.com/en-us/108794) | Official device activation documentation |
| [Secure Enclave](https://support.apple.com/guide/security/sec59b0b31ff) | SEP architecture and activation state storage |
| [The Apple Wiki -- Firmware Keys](https://theapplewiki.com/wiki/Firmware_Keys) | Decryption keys for IPSW ramdisk extraction |
| [The iPhone Wiki -- Activation Token](https://www.theiphonewiki.com/wiki/Activation_Token) | Activation record plist format and fields |
| [The iPhone Wiki -- Setup.app](https://www.theiphonewiki.com/wiki//Applications/Setup.app) | Setup Assistant paths targeted by deletescript |
| [Albert Server](https://theapplewiki.com/wiki/Albert) | Apple's activation server endpoints |
