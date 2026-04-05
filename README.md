# tr4mpass: iCloud Activation Lock Bypass

Free, open-source tool that removes iCloud activation lock from iPhones and iPads. Runs locally on your computer -- no servers, no payments, no accounts needed. Just plug in your device and run.

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
- Run the bypass
- Tell you when it's done

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

| Device | Chip | Bypass Method | Signal Preserved |
|--------|------|---------------|------------------|
| iPhone 5s | A7 | Path A (checkm8) | No (WiFi only) |
| iPhone 6 / 6 Plus | A8 | Path A (checkm8) | No (WiFi only) |
| iPhone 6s / 6s Plus | A9 | Path A (checkm8) | Yes |
| iPhone SE (1st gen) | A9 | Path A (checkm8) | Yes |
| iPhone 7 / 7 Plus | A10 | Path A (checkm8) | Yes |
| iPhone 8 / 8 Plus | A11 | Path A (checkm8) | Yes |
| iPhone X | A11 | Path A (checkm8) | Yes |
| iPhone XR | A12 | Path B (session) | Yes |
| iPhone XS / XS Max | A12 | Path B (session) | Yes |
| iPhone 11 / Pro / Pro Max | A13 | Path B (session) | Yes |
| iPhone SE (2nd gen) | A13 | Path B (session) | Yes |
| iPhone 12 / Mini / Pro / Pro Max | A14 | Path B (session) | Yes |
| iPhone 13 / Mini / Pro / Pro Max | A15 | Path B (session) | Yes |
| iPhone SE (3rd gen) | A15 | Path B (session) | Yes |
| iPhone 14 / Plus | A15 | Path B (session) | Yes |
| iPhone 14 Pro / Pro Max | A16 | Path B (session) | Yes |
| iPhone 15 / Plus | A16 | Path B (session) | Yes |
| iPhone 15 Pro / Pro Max | A17 | Path B (session) | Yes |

### iPad

| Device | Chip | Bypass Method |
|--------|------|---------------|
| iPad Air | A7 | Path A (checkm8) |
| iPad Air 2 | A8X | Path A (checkm8) |
| iPad mini 4 | A8 | Path A (checkm8) |
| iPad Pro (1st gen) | A9X | Path A (checkm8) |
| iPad (5th-7th gen) | A9-A10 | Path A (checkm8) |
| iPad Pro (2nd gen) | A10X | Path A (checkm8) |
| iPad Pro (3rd/4th gen) | A12/A12Z | Path B (session) |
| iPad Air (4th/5th gen) | A14/M1 | Path B (session) |
| iPad Pro (5th/6th gen) | M1/M2 | Path B (session) |

### Other

| Device | Chip | Bypass Method |
|--------|------|---------------|
| iPod touch (6th gen) | A8 | Path A (checkm8) |
| iPod touch (7th gen) | A10 | Path A (checkm8) |
| Apple TV 4K (1st gen) | A10X | Path A (checkm8) |
| Apple Watch Series 1-3 | S1P/S3 | Path A (checkm8) |

---

## What Does "Signal Preserved" Mean?

- **Yes** -- After bypass, your iPhone keeps full cellular service (calls, texts, data). SIM card works normally.
- **No (WiFi only)** -- After bypass, the device works on WiFi but cellular service is disabled. This applies to older devices (A7/A8) and all WiFi-only iPads.

---

## Advanced Usage

If you prefer to run things manually instead of using `start.sh`:

```bash
# Build
make

# Just identify your device (no bypass)
./tr4mpass --detect-only

# Run with full debug output
./tr4mpass --verbose

# Force a specific bypass path
./tr4mpass --force-path-a    # checkm8 (A5-A11 devices)
./tr4mpass --force-path-b    # session activation (A12+ devices)

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
    libimobiledevice-dev libirecovery-dev libusb-1.0-0-dev \
    libplist-dev libssl-dev pkg-config build-essential
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

## How It Works (Technical)

tr4mpass has two bypass paths, selected automatically based on your device's chip:

### Path A -- checkm8 (A5 through A11)

Uses a permanent hardware vulnerability in the BootROM (the very first code that runs when the device powers on). This vulnerability cannot be patched by Apple because the BootROM is burned into silicon at the factory.

```
DFU Mode -> checkm8 exploit -> load ramdisk -> jailbreak kernel
-> inject activation record -> cleanup Setup.app -> verify
```

### Path B -- Session Activation (A12 and later)

Does not use a hardware exploit. Instead, it manipulates the device's identity in DFU mode, then uses Apple's own activation protocol (drmHandshake session mode) to change the lock state.

```
DFU Mode -> identity manipulation -> signal detection
-> session handshake -> activation record -> cleanup -> verify
```

### Architecture

```
+-------------------------+
|        start.sh         |  Guided wrapper (OS detect, deps, DFU guide)
+-------------------------+
|         main.c          |  CLI, device detection, path selection
+-------------------------+
|  Path A    |   Path B   |  Bypass strategies
+------------+------------+
| checkm8    | identity   |  Exploit / manipulation
| jailbreak  | session    |  Activation protocol
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
| `src/exploit/` | checkm8 exploit stages, DFU protocol, payload assembly |
| `src/bypass/` | Path A, Path B, signal detection, deletescript, AFC utils |
| `src/activation/` | Activation client, session protocol, record builder |
| `src/util/` | Logging, plist helpers, USB helpers |

---

## iOS 26.3+ (Research in Progress)

Current bypass methods work up to iOS 26.1. For iOS 26.2 and later, Apple hardened several security layers that break existing approaches.

We have identified a potential bypass vector through **VU#346053** -- an unpatched vulnerability in Apple's activation server that accepts unsigned XML payloads. This could enable bypass via a captive portal during device setup, without needing any exploit or jailbreak.

**This is not implemented yet.** See [`26.3-vulnerability.md`](26.3-vulnerability.md) for the full technical analysis, proposed attack chain, and implementation roadmap.

---

## Disclaimer

This tool is for **authorized security research only** (Apple Security Bounty program).

- Only use on devices you own or have written authorization to test.
- Bypassing activation lock on stolen devices is illegal.
- No warranty. Use at your own risk.
- Results are not guaranteed for every device or iOS version.

---

## License

MIT
