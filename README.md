# tr4mpass: Open-Source iOS Activation Lock Bypass Tool

tr4mpass is a terminal-based C tool that removes iCloud activation lock from
iOS devices locally. No third-party servers, no paid services, no account
credentials required. Everything runs on your machine over a USB cable.

Built for **Apple Security Bounty** research. Target: **iOS <= 26.1**.

---

## The Problem

An **activation lock** is Apple's anti-theft mechanism tied to iCloud. When
enabled, the device demands the original owner's Apple ID before it can be
used. This is useful when a device is stolen. It is not useful when:

- You bought a used device and the seller forgot to remove the lock.
- You are a security researcher testing iOS activation infrastructure.
- You work in IT asset recovery or device refurbishment.
- You inherited a device and cannot contact the original owner.

Apple offers no self-service path for these cases. Their official process
requires proof of purchase and an appointment at an Apple Store, which may
not exist in your country, and frequently results in rejection.

tr4mpass provides a **local, offline bypass** that works without contacting
Apple or any third party.

---

## How It Works

```
Connect Device --> Detect Chip --> Enter DFU --> Exploit --> Bypass --> Verify
```

tr4mpass supports two bypass paths, selected automatically based on the
device's SoC:

**Path A -- checkm8 (A5 through A11)**

Uses a permanent, unfixable hardware vulnerability in the BootROM
(checkm8) to gain code execution at the lowest level. From there, it loads
a ramdisk, applies a jailbreak, injects a local activation record, runs
cleanup, and verifies the lock is removed. Apple cannot patch this because
the BootROM is read-only silicon.

Chain: `DFU -> checkm8 exploit -> ramdisk -> jailbreak -> activation
record injection -> deletescript cleanup -> verify`

**Path B -- Session Activation (A12 and later)**

Does not use a BootROM exploit. Instead, it manipulates the device's
identity descriptors in DFU mode, detects signal type (cellular vs
WiFi-only), then uses the drmHandshake session-mode activation protocol to
change the lock state. Runs cleanup and verifies.

Chain: `DFU -> identity manipulation -> signal detection -> drmHandshake
session activation -> deletescript cleanup -> verify`

---

## Supported Devices

### Path A -- checkm8 Devices (A5 through A11)

These devices have a permanent BootROM vulnerability. Fully supported.

| Chip | CPID | Devices | Notes |
|------|------|---------|-------|
| A7 | 0x8947 | iPhone 5s, iPad Air | ARMv7 payload |
| A9 (TSMC) | 0x8000 | iPhone 6s, iPhone SE | ARM64 |
| A9 (Samsung) | 0x8003 | iPhone 6s (Samsung variant) | ARM64 |
| A9 | 0x8950 | Apple TV (3rd gen) | ARMv7 payload |
| A9 | 0x8955 | Apple TV (3rd gen, rev A) | ARMv7 payload |
| S1P | 0x7002 | Apple Watch Series 1/2 | ARMv7 payload |
| S3 | 0x8002 | Apple Watch Series 3 | ARMv7, ROP (hole=5) |
| S3 | 0x8004 | Apple Watch Series 3 (GPS+Cellular) | ARMv7, ROP (hole=5) |
| A8 | 0x7000 | iPod touch 6G, iPad mini 4 | ARM64 |
| A8X | 0x7001 | iPad Air 2 | ARM64 |
| A9X | 0x8001 | iPad Pro (1st gen) | ARM64, ROP gadgets |
| A10 | 0x8960 | iPhone 7, iPhone 7 Plus | ARM64, large leak |
| A10 | 0x8010 | iPhone 7, iPod touch 7G | ARM64, ROP gadgets |
| A10X | 0x8011 | iPad Pro 10.5", Apple TV 4K | ARM64, ROP gadgets |
| A11 | 0x8015 | iPhone 8, iPhone 8 Plus, iPhone X | ARM64, ROP gadgets |
| T2 | 0x8012 | Apple T2 Security Chip | ARM64, ROP gadgets |

### Path B -- Session Activation Devices (A12 and later)

These devices are NOT checkm8-vulnerable. Bypass uses identity
manipulation and session-mode activation instead.

| Chip | CPID | Devices | Status |
|------|------|---------|--------|
| A12 | 0x8020 | iPhone XS, iPhone XR | Supported |
| A12Z | 0x8027 | iPad Pro (4th gen) | Supported |
| A13 | 0x8030 | iPhone 11, iPhone 11 Pro | Supported |
| A14 | 0x8101 | iPhone 12, iPad Air (4th gen) | Supported |
| M1 | 0x8103 | iPad Pro (5th gen), MacBook (M1) | Supported |
| A15 | 0x8110 | iPhone 13, iPhone SE (3rd gen) | Supported |
| M2 | 0x8112 | iPad Pro (6th gen), MacBook (M2) | Supported |
| A16 | 0x8120 | iPhone 14 Pro, iPhone 14 Pro Max | Supported |
| A17 | 0x8130 | iPhone 15 Pro, iPhone 15 Pro Max | Supported |

---

## Quick Start

The fastest way to get started is the guided wrapper script, which handles
dependency installation, building, device detection, DFU guidance, and
bypass execution:

```bash
git clone https://github.com/tr4m0ryp/tr4mpass.git
cd tr4mpass
./start.sh
```

The script auto-detects your OS and installs everything needed.

<details>
<summary>Manual Install -- macOS</summary>

Requires Homebrew.

```bash
brew install libimobiledevice libirecovery libusb libplist openssl pkg-config
git clone https://github.com/tr4m0ryp/tr4mpass.git
cd tr4mpass
make
```

</details>

<details>
<summary>Manual Install -- Linux (Debian/Ubuntu)</summary>

```bash
sudo apt-get update
sudo apt-get install -y \
    libimobiledevice-dev libirecovery-dev libusb-1.0-0-dev \
    libplist-dev libssl-dev pkg-config build-essential

git clone https://github.com/tr4m0ryp/tr4mpass.git
cd tr4mpass
make
```

</details>

<details>
<summary>Manual Install -- Linux (Fedora/RHEL)</summary>

```bash
sudo dnf install -y \
    libimobiledevice-devel libirecovery-devel libusb1-devel \
    libplist-devel openssl-devel pkg-config gcc make

git clone https://github.com/tr4m0ryp/tr4mpass.git
cd tr4mpass
make
```

</details>

<details>
<summary>Manual Install -- Windows (via WSL)</summary>

Native Windows is not supported. Use Windows Subsystem for Linux.

```powershell
wsl --install
```

Then open a WSL terminal and follow the Linux (Debian/Ubuntu) instructions
above.

</details>

---

## Usage

### Guided Mode (Recommended)

```bash
./start.sh
```

The script walks you through the entire process: connects to your device,
identifies the chip, guides you into DFU mode if needed, selects the
correct bypass path, and executes it.

### Direct CLI

```bash
./tr4mpass                   # Auto-detect device and bypass path
./tr4mpass --detect-only     # Identify the device and exit
./tr4mpass --verbose         # Full debug output
./tr4mpass --force-path-a    # Force checkm8 path (A5-A11)
./tr4mpass --force-path-b    # Force session path (A12+)
./tr4mpass --dry-run         # Show what would happen without executing
./tr4mpass --help            # Show all options
```

### CLI Options

| Flag | Short | Description |
|------|-------|-------------|
| `--verbose` | `-v` | Enable debug-level logging |
| `--dry-run` | `-n` | Print the selected module without executing |
| `--detect-only` | `-d` | Detect and print device info, then exit |
| `--force-path-a` | `-a` | Force Path A (checkm8) regardless of chip |
| `--force-path-b` | `-b` | Force Path B (session) regardless of chip |
| `--help` | `-h` | Print usage information |

---

## Architecture

```
+-------------------------+
|        start.sh         |  Guided wrapper (OS detect, deps, DFU guide)
+-------------------------+
|         main.c          |  CLI parsing, device detect, module select
+-------------------------+
|  Path A    |   Path B   |  Bypass strategy modules
+------------+------------+
| checkm8    | identity   |  Exploit / manipulation layer
| jailbreak  | session    |  Activation protocol layer
+------------+------------+
| activation | record     |  Activation record handling
+------------+------------+
| dfu_proto  | usb_dfu    |  USB/DFU protocol layer
+------------+------------+
| libusb | libimobiledevice | libplist | OpenSSL |  Dependencies
+--------+------------------+---------+---------+
```

### Module Map

| Module | Header | Source | Purpose |
|--------|--------|--------|---------|
| Core | `include/tr4mpass.h` | `src/main.c` | Version, entry point, orchestration |
| Bypass Framework | `include/bypass/bypass.h` | `src/bypass/bypass.c` | Plugin registry, module selection |
| Path A | `include/bypass/path_a.h` | `src/bypass/path_a.c`, `path_a_jailbreak.c` | checkm8 chain (A5-A11) |
| Path B | `include/bypass/path_b.h` | `src/bypass/path_b.c`, `path_b_identity.c` | Session activation (A12+) |
| Signal | `include/bypass/signal.h` | `src/bypass/signal.c` | Cellular vs WiFi-only detection |
| Deletescript | `include/bypass/deletescript.h` | `src/bypass/deletescript.c` | Post-bypass cleanup |
| AFC Utils | `include/bypass/afc_utils.h` | `src/bypass/afc_utils.c` | Apple File Conduit helpers |
| Device | `include/device/device.h` | `src/device/device.c` | Device detection and info |
| USB DFU | `include/device/usb_dfu.h` | `src/device/usb_dfu.c` | DFU mode USB communication |
| Chip DB | `include/device/chip_db.h` | `src/device/chip_db.c` | SoC lookup table (A5-A17) |
| checkm8 | `include/exploit/checkm8.h` | `src/exploit/checkm8.c`, `checkm8_stages.c`, `checkm8_patch.c`, `checkm8_payload.c` | BootROM exploit implementation |
| DFU Proto | `include/exploit/dfu_proto.h` | `src/exploit/dfu_proto.c` | DFU protocol operations |
| Activation | `include/activation/activation.h` | `src/activation/activation.c` | Activation flow orchestration |
| Session | `include/activation/session.h` | `src/activation/session.c` | drmHandshake session mode |
| Record | `include/activation/record.h` | `src/activation/record.c` | Activation record construction |
| Logging | `include/util/log.h` | `src/util/log.c` | Leveled logging |
| Plist | `include/util/plist_helpers.h` | `src/util/plist_helpers.c` | libplist convenience wrappers |
| USB | `include/util/usb_helpers.h` | `src/util/usb_helpers.c` | libusb convenience wrappers |

---

## Signal vs No-Signal

tr4mpass detects whether a device has cellular capability (GSM, CDMA, or
dual-mode) based on IMEI and MEID fields.

- **Cellular devices** (iPhone, iPad Cellular): The bypass preserves cell
  service. After bypass, the device functions normally with full cellular
  connectivity.

- **WiFi-only devices** (iPad WiFi, iPod touch): The bypass removes the
  activation lock for WiFi usage. There is no cellular radio to preserve.

Signal detection is automatic. The `signal_detect_type()` function
classifies the device and the bypass path adjusts its activation record
strategy accordingly.

---

## iOS 26.3+ Research

**Status: NOT IMPLEMENTED -- research only.**

Current bypass paths (A and B) work on iOS versions up to 26.1. Starting
with iOS 26.2, Apple hardened server-mandatory activation, MEID-based
record signing, Secure Enclave activation state, and code signing
enforcement in ways that break existing techniques.

A potential bypass vector for iOS 26.2+ has been identified through
**VU#346053** -- an unpatched XML payload injection vulnerability in
Apple's activation endpoint (`humb.apple.com/humbug/baa`), discovered by
security researcher JGoyd and reported to Apple and US-CERT in May 2025.
As of April 2026, it remains unpatched.

This would operate as a **Path C** in tr4mpass's architecture, using a
captive portal / rogue access point to inject crafted XML provisioning
payloads during the Setup Assistant phase. No jailbreak or BootROM exploit
required.

See `26.3-vulnerability.md` for technical details, proposed attack chain,
security layer analysis, and implementation roadmap.

---

## Building from Source

Requirements:
- C99 compiler (GCC or Clang)
- pkg-config
- libimobiledevice (>= 1.0)
- libirecovery (>= 1.0)
- libusb (>= 1.0)
- libplist (>= 2.0)
- OpenSSL

```bash
make          # Build
make clean    # Remove build artifacts
```

The Makefile auto-discovers all `.c` files under `src/` via `find`. No
manual source list maintenance needed. Headers are included from
`include/` via `-Iinclude`. Library flags are resolved through pkg-config.

---

## Disclaimer

This tool is developed for **authorized security research only**, in the
context of Apple's Security Bounty program. It is intended to demonstrate
and document vulnerabilities in iOS activation infrastructure.

- Only use this tool on devices you personally own or have explicit written
  authorization to test.
- Bypassing activation lock on stolen devices is illegal in most
  jurisdictions.
- The authors are not responsible for misuse of this tool.
- No warranty of any kind is provided. Use at your own risk.
- This tool does not guarantee a successful bypass on every device or iOS
  version.

---

## License

MIT
