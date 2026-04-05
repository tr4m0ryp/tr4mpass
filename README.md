# tr4mpass: iCloud Activation Lock Bypass

Free, open-source tool that removes iCloud activation lock from iPhones and iPads. Runs locally on your computer -- no servers, no payments, no accounts needed. Just plug in your device and run.

**Supports iOS 12 through 26.1 | iPhone 5s through iPhone 15 Pro**

***For developers and security researchers: read [`26.3-vulnerability.md`](26.3-vulnerability.md) for potential vulnerabilities that could enable bypass on iOS 26.3 and later.***

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

## References

This project was built on extensive research and analysis of open-source tools, security disclosures, Apple documentation, and community knowledge.

### Open-Source Tools and Libraries

| Project | What we used it for |
|---------|-------------------|
| [gaster](https://github.com/0x7ff/gaster) | checkm8 exploit implementation in C -- chip offsets, ROP chains, exploit stages, shellcode structure |
| [ipwndfu](https://github.com/axi0mX/ipwndfu) | Original checkm8 exploit reference (Python) -- payload structure, heap spray parameters |
| [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice) | Device communication, lockdownd, mobileactivation API |
| [libideviceactivation](https://github.com/libimobiledevice/libideviceactivation) | Activation record handling, drmHandshake protocol |
| [go-ios](https://github.com/danielpaulus/go-ios) | mobileactivation module -- session protocol, handshake sequence, activation flow |
| [pymobiledevice3](https://github.com/doronz88/pymobiledevice3) | Python reference for activation protocol and mobile services |
| [checkra1n / PongoOS](https://github.com/checkra1n/PongoOS) | Pre-boot execution environment, jailbreak reference for A5-A11 |
| [palera1n](https://github.com/palera1n/palera1n) | Modern checkm8-based jailbreak, ramdisk builder |
| [jbinit](https://github.com/tihmstar/jbinit) | iOS booter ramdisk creator for checkm8 devices |
| [Lockra1n](https://github.com/alwaysappleftd/Lockra1n_v2.0) | Untethered iCloud bypass reference (iOS 15-16.7.8) |
| [LIBRE-HACKTIVATOR](https://github.com/i3T4AN/LIBRE-HACKTIVATOR_iOS_12-16) | Deletescript reference -- per-iOS-version cleanup scripts |
| [iOS-Hacktivation-Toolkit](https://github.com/Hacktivation/iOS-Hacktivation-Toolkit) | Per-version mobileactivationd patching scripts |
| [Onii_Ramdisk](https://github.com/LinhOnii/Onii_Ramdisk) | Free iCloud bypass ramdisk tool |
| [img4tool](https://github.com/Tihmstar/img4tool) | IMG4 firmware container extraction |
| [iBoot64Patcher](https://github.com/Tihmstar/iBoot64Patcher) | Bootloader patching for ramdisk loading |
| [ramdiskutil](https://github.com/cfw-project/ramdiskutil) | iOS restore ramdisk customization |
| [iExtractor](https://github.com/malus-security/iextractor) | Automated IPSW firmware extraction |

### Security Research and Disclosures

| Source | Relevance |
|--------|-----------|
| [VU#346053 -- iOS Activation Flaw](https://github.com/JGoyd/iOS-Activation-Flaw) | Unpatched XML injection at humb.apple.com/humbug/baa (iOS 26.3+ bypass vector) |
| [Full Disclosure: iOS Activation Flaw](https://seclists.org/fulldisclosure/2025/Jun/27) | Original public disclosure of VU#346053 (June 2025) |
| [CVE-2019-8900 -- checkm8](https://theapplewiki.com/wiki/Checkm8_Exploit) | BootROM use-after-free vulnerability (A5-A11) |
| [CVE-2023-41991 -- CoreTrust](https://theapplewiki.com/wiki/CoreTrust) | Code signing bypass (patched iOS 17.0.1), explains why persistent signing fails on iOS 18+ |
| [CVE-2026-20687 -- Kernel UAF](https://support.apple.com/en-us/100100) | IOCommandGate use-after-free via AppleKeyStore race condition (iOS 26.1-26.3) |
| [CVE-2026-28859 -- WebKit Sandbox Escape](https://support.apple.com/en-us/100100) | Memory handling bug allowing process to escape sandbox (patched iOS 26.4) |

### Apple Documentation

| Source | What it covers |
|--------|---------------|
| [Apple Activation Lock](https://support.apple.com/en-us/108794) | Official activation lock documentation |
| [Apple Security Bounty](https://security.apple.com/bounty/) | Bug bounty program this research falls under |
| [Secure Enclave](https://support.apple.com/guide/security/sec59b0b31ff) | SEP architecture and activation state storage |
| [The Apple Wiki -- Firmware Keys](https://theapplewiki.com/wiki/Firmware_Keys) | Decryption keys for IPSW ramdisk extraction |
| [The iPhone Wiki -- Activation Token](https://www.theiphonewiki.com/wiki/Activation_Token) | Activation record plist format and fields |
| [The iPhone Wiki -- WildcardTicket](https://www.theiphonewiki.com/wiki/WildcardTicket) | Activation ticket structure (TEA-CBC encryption) |
| [The iPhone Wiki -- Setup.app](https://www.theiphonewiki.com/wiki//Applications/Setup.app) | Setup Assistant paths targeted by deletescript |
| [Albert Server](https://theapplewiki.com/wiki/Albert) | Apple's activation server endpoints |

### Analysis and Articles

| Source | Topic |
|--------|-------|
| [GBHackers -- iOS Activation Flaw](https://gbhackers.com/apple-ios-activation-flaw-enables-injection/) | VU#346053 technical analysis |
| [CyberSecurity News -- Activation Vulnerability](https://cybersecuritynews.com/apples-ios-activation-vulnerability/) | VU#346053 impact analysis |
| [checkm8 Deep Dive (Medium)](https://medium.com/@saadanashraf86/the-unpatchable-flaw-a-deep-dive-into-apples-dfu-mode-and-the-checkm8-exploit-0d3dae2a2075) | checkm8 exploit mechanics |
| [Zimperium -- checkm8 Analysis](https://zimperium.com/blog/zimperium-analysis-of-checkm8) | Security industry analysis of checkm8 |
| [Habr -- checkm8 Technical Analysis](https://habr.com/en/companies/dsec/articles/472762/) | Detailed technical breakdown of the DFU UAF |
| [Black Hat 2016 -- Demystifying the SEP](https://blackhat.com/docs/us-16/materials/us-16-Mandt-Demystifying-The-Secure-Enclave-Processor.pdf) | Secure Enclave architecture research |
| [Dynastic Research -- iOS Codesign Bypass](https://research.dynastic.co/2019/03/01/codesign-bypass) | CoreTrust/AMFI bypass history |

### Commercial Tools Analyzed

During development, the following proprietary tools were analyzed to understand bypass flows and protocol implementations:

| Tool | Version | What we extracted |
|------|---------|------------------|
| Checkm8.info Software | 9.5 | Two-section architecture (A5-A11 vs A12+), DFU exploit flow, FActivation protocol, offline bypass method, bundled go-ios binary, ipwndfu payloads |
| iRemoveTools | 9.5 | A12+ activation APIs, signal vs no-signal handling, MobileDeviceFramework usage, mobileactivationd interaction |

---

## License

MIT
