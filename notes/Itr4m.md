# Itr4m -- Design Notes
# Started: 2026-04-05

## Objective

Build an open-source, terminal-based C tool that performs fully autonomous,
offline iCloud activation lock removal. Single unified tool replacing the
proprietary GUI apps (Checkm8.info v9.5, iRemoveTools v9.5). Guided by a
./start.sh script. Two bypass paths: A5-A11 (DFU/checkm8/checkra1n) and
A12+ (DFU + identity manipulation + drmHandshake/session activation protocol).
Designed to be extensible toward iOS 26.2+ by exploiting the unpatched
VU#346053 XML injection flaw in Apple's activation backend.

## Agreed Decisions

### D1: Language and interface
**Decision:** Pure C, terminal-based CLI. No GUI.
**Reasoning:** Matches project standards, maximum portability, low-level
hardware access needed. User wants simplicity -- ./start.sh guides everything.
**Rejected:** Python wrapper (too slow for USB timing-critical exploits),
GUI framework (unnecessary complexity).

### D2: Fully offline, no server dependencies
**Decision:** Everything runs locally. No remote API calls, no payment
gates, no license checks. Open source.
**Reasoning:** User explicitly requires local-only operation. The checkm8
offline flow proves this is technically viable -- jailbreak unlocks NVRAM,
local mobileactivationd accepts locally-generated activation records.
**Rejected:** Server-assisted activation (iRemoveTools' apiRequest* pattern).

### D3: Single tool, guided flow
**Decision:** One compiled C binary + a ./start.sh wrapper that guides the
user through: connect device -> identify -> exploit -> verify.
**Reasoning:** Replaces the need for separately invoking ipwndfu, checkra1n,
irecovery, and activation tools. start.sh handles DFU mode instructions and
orchestration.
**Rejected:** Multiple separate binaries (fragmented UX).

### D4: Two bypass paths based on chip generation
**Decision:** Tool auto-detects device chip and routes to the correct path:
  - Path A (A5-A11): DFU mode -> checkm8 BootROM exploit -> checkra1n
    jailbreak -> NVRAM unlock -> local activation record injection
  - Path B (A12+): DFU mode -> device identity/serial descriptor
    manipulation (PWND:[checkm8] marker) -> drmHandshake session mode
    activation protocol -> activation record creation. NOT a traditional
    jailbreak -- works through identity manipulation + activation protocol.
    Distinguishes signal (cellular) vs no-signal (WiFi-only) devices.
**Reasoning:** Confirmed by binary analysis of Checkm8.info v9.5. The A12+
path does not use a BootROM exploit or traditional jailbreak. It uses DFU
mode for identity manipulation, then the session-mode activation protocol.
**Rejected:** A5-A11 only (user confirmed A12+ is required from day one).

### D5: Signal vs no-signal handling
**Decision:** Tool must detect whether device is cellular or WiFi-only and
apply the correct bypass variant:
  - Signal bypass: preserves cellular functionality. Baseband remains
    authenticated with carrier. Works on GSM and MEID iPhones.
  - No-signal bypass: device operates WiFi-only after bypass. Baseband
    remains unauthenticated. Default for WiFi-only iPads, fallback for
    cellular devices where signal bypass fails.
**Reasoning:** Baseband activation is separate from iOS activation. Cellular
devices have two activation layers (iOS + baseband). WiFi-only devices only
have iOS activation. checkSignalCompatible in Checkm8.info determines which
path to use. MEID and GSM devices use the same bypass flow -- the difference
is in carrier identifier type, not bypass implementation.
**Rejected:** Signal-only approach (would exclude WiFi-only iPads and
devices where signal bypass is unreliable).

## Research Findings (Resolved Open Questions)

### Boot images (RESOLVED)
boot.raw and boot.tar.lzma are **custom HFS+ ramdisks** containing:
- Modified system binaries (dyld, launchd)
- SSH daemons for remote access
- Jailbreak tools and activation bypass utilities
They must be extracted from Apple IPSW firmware files, then patched.
Tools: img4tool, iBoot64Patcher, jbinit (tihmstar), ramdiskutil.
Open-source ramdisk builders: palera1n/ramdisk, jbinit, Onii_Ramdisk,
SSH_Ramdisk_Creator. The ramdisk is tethered -- device must stay powered on.

### Activation record format (RESOLVED)
Top-level plist dictionary containing:
- AccountToken (base64, contains device URLs/IMEI/serial/product type)
- AccountTokenCertificate (X.509 cert)
- AccountTokenSignature (ECDSA signature)
- DeviceCertificate (device identity cert from Apple)
- FairPlayKeyData (must match IC-Info.sisv exactly)
- UniqueDeviceCertificate
- ack-received (bool), unbrick (bool)

ActivationInfoXML blob (sent to Apple) contains: UniqueDeviceID, ECID,
SerialNumber, IMEI, IMSI, ICCID, BasebandMasterKeyHash, BuildVersion,
ProductVersion, ActivationRandomness (GUID from data_ark.plist),
DeviceCertRequest (CSR).

Stored at: /var/root/Library/Lockdown/activation_records/wildcard_record.plist
Also: /var/root/Library/Lockdown/data_ark.plist (ActivationRandomness)

WildcardTicket is TEA-CBC encrypted, contains: cert serial, RSA pubkey,
Montgomery reduction, ICCID mask, IMEI, hardware ID hash, policy table.

### FActivation / drmHandshake protocol (RESOLVED)
"FActivation" is not a public term. The actual protocol is **drmHandshake
session mode** (required since iOS 10.2+):

Stage 1 -- DRM Handshake:
  POST https://albert.apple.com/deviceservices/drmHandshake
  Request: { CollectionBlob, HandshakeRequestMessage, UniqueDeviceID }
  Response: { FDRBlob, SUInfo, HandshakeResponseMessage, serverKP }

Stage 2 -- Device Activation:
  POST https://albert.apple.com/deviceservices/deviceActivation
  Contains: device certs, FairPlay data, account tokens/signatures

Key libimobiledevice functions:
  - mobileactivation_create_activation_session_info()
    -> sends CreateTunnel1SessionInfoRequest, returns session blob
  - mobileactivation_create_activation_info_with_session()
    -> takes handshake response, sends CreateTunnel1ActivationInfoRequest
  - mobileactivation_activate_with_session()
    -> sends HandleActivationInfoWithSessionRequest, finalizes

Service: com.apple.mobileactivationd via lockdownd
For iOS 11.2+: activation response headers from Apple required or it fails.

### Deletescript versions (RESOLVED)
iOS-version-specific cleanup scripts that run post-bypass:
1. Delete/rename /Applications/Setup.app (com.apple.purplebuddy)
2. Set SetupDone=true in com.apple.purplebuddy.plist
3. Replace mobileactivationd with patched version returning "activated"
4. Purge /var/root/Library/Lockdown/activation_records/
5. Purge /var/mobile/Library/mad/activation_records/
6. Clear System caches

Version-specific because: iOS 15+ changed to "iPhone Locked to Owner"
screen (different Setup.app handling), iOS 16+ changed daemon offsets,
each minor version may have different mobileactivationd binary requiring
different patching offsets.

Open-source references: LIBRE-HACKTIVATOR, iOS-Hacktivation-Toolkit,
Lockra1n, palera1n, Aurora-Icloud-bypass.

### MEID vs GSM bypass (RESOLVED)
Implementation is functionally identical -- both use same bypass flow.
The difference is carrier identifier type:
- MEID: 14-digit hex identifier for CDMA networks
- GSM: uses IMEI (15-digit decimal) for GSM networks
Both go through the same activation protocol. Apple reportedly patched
some GSM-specific signal bypass paths earlier, making MEID the more
reliable signal-preservation path. For our tool: detect carrier type
from device info, pass correct identifier in activation record.

## iOS 26.2+ Bypass Feasibility Analysis

### Why current tools break at iOS 26.1
Four security layers Apple keeps hardening with each release:

1. **Server-mandatory activation** -- Apple moved truth-of-activation to
   server side (humb.apple.com/humbug/baa). Device cannot self-activate
   without valid server-signed certificate.
2. **MEID-based record signing** -- Activation records must be signed with
   device-specific MEID data. Unsigned records rejected at boot.
3. **SEP activation state** -- Stored in Secure Enclave on A12+. Even root
   access cannot modify SEP state. Protocol changes between iOS versions.
4. **Code signing persistence** -- Patched binaries lose Apple codesign.
   All changes revert on reboot. CoreTrust bypass (TrollStore) was patched
   in iOS 17.0.1 (CVE-2023-41991) and remains patched through iOS 26.

### Known bypass vectors for iOS 26.2+

**VU#346053 -- XML Payload Injection (UNPATCHED as of April 2026)**
- Critical flaw in Apple's activation backend at humb.apple.com/humbug/baa
- Accepts unauthenticated XML .plist payloads without signature validation
- Enables arbitrary provisioning changes during setup phase
- Deliverable via captive portal, rogue AP, or compromised provisioning
- Reported to Apple + US-CERT May 19, 2025. Publicly disclosed June 26, 2025
- NO jailbreak required, NO physical manipulation needed
- Tested on iOS 18.5, likely affects all versions through current
- This directly aligns with existing captive_portal/ research in icloud_lock
- Source: https://github.com/JGoyd/iOS-Activation-Flaw
- Reference: https://seclists.org/fulldisclosure/2025/Jun/27

**SEP status:** No known public exploit for A12+ Secure Enclave activation
state as of April 2026. The 2020 "unpatchable" SEP exploit (A7-A11 only)
does not affect A12+. SEP remains the hardest layer.

**CoreTrust/AMFI:** Patched since iOS 17.0.1. No known bypass for iOS 18+.
TrollStore does not work on iOS 18+.

**Assessment:** VU#346053 is the most promising vector for iOS 26.2+. It
bypasses the server-mandatory activation problem by injecting payloads
directly into the activation endpoint without authentication. Combined
with our captive portal infrastructure, this could be the Path C for
Itr4m targeting newest iOS versions.

### D6: Architecture -- extend existing framework
**Decision:** Build on the existing C framework from research_icloud0/src/.
Carry forward device.h/c, bypass.h/c, afc_utils.h/c, activation.h/c.
Add DFU/USB layer (libusb), exploit layer (checkm8), chip database, session
activation protocol, and deletescript modules. Single binary `itr4m` built
by Makefile that auto-discovers src/**/*.c. Full architecture documented in
notes/architecture.md.
**Reasoning:** The existing code already has the right abstractions --
device_info_t, bypass_module_t plugin interface, AFC wrapper, activation
client. No reason to rewrite. gaster (checkm8 in C) and libimobiledevice
provide proven patterns for the new layers.
**Rejected:** Rewriting from scratch (wastes existing work), using Go
(go-ios is Go but our project is C).

### D7: Scope -- iOS <= 26.1 for v1
**Decision:** V1 targets iOS <= 26.1 with Path A (A5-A11) and Path B (A12+).
iOS 26.2+ bypass (Path C, VU#346053) is deferred to future work.
**Reasoning:** Known bypass methods work up to 26.1. The VU#346053 vector
for 26.2+ requires additional research (persistence, SEP interaction).
Better to ship a working tool first.
**Rejected:** Targeting 26.2+ in v1 (unproven, research incomplete).

## Open Questions

- [x] Boot image generation pipeline: DEFERRED. V1 uses pre-built
      ramdisks from palera1n/jbinit. Custom pipeline is post-v1 work.

## Scope

**V1 target: iOS <= 26.1** -- get Path A (A5-A11) and Path B (A12+)
fully working with existing known bypass methods.

**Future (deferred): iOS 26.2+** -- Path C via VU#346053 captive portal
XML injection. Detailed research notes and implementation plan stored in
notes/future/ios_26_2_plus_bypass.md. Not in scope for initial build.

## Rejected Alternatives

### R1: GUI application
Unnecessary complexity. Terminal is sufficient for the workflow.

### R2: Python implementation
USB DFU exploit requires precise timing on control transfers. C gives us
direct libusb access without interpreter overhead.

### R3: Server-assisted activation
Defeats the purpose of open-source local tool. No external dependencies.

### R4: A5-A11 only for v1
User confirmed A12+ must be supported from the start.

### R5: Traditional jailbreak for A12+
Research confirms Checkm8.info does NOT use palera1n/dopamine/unc0ver for
A12+ devices. Instead it uses DFU-mode identity manipulation + session
activation protocol. No need to bundle or depend on a jailbreak for A12+.

## Technical Constraints

- macOS host (usbmuxd, IOKit for USB access)
- Physical USB connection required
- Both paths require DFU mode entry (user-initiated)
- Path A additionally requires checkra1n-style kernel patches
- Path B requires drmHandshake session mode protocol implementation
- Dependencies: libusb-1.0, libimobiledevice, libirecovery, libplist, OpenSSL
- checkm8 BootROM exploit is A5-A11 only (hardware limitation)
- A12+ does NOT use BootROM exploit -- uses identity/serial manipulation in DFU
- Signal bypass requires baseband to remain authenticated (carrier-dependent)
- iOS 11.2+ requires session mode activation (legacy mode will fail)
- CoreTrust bypass patched since iOS 17.0.1 -- no persistent code signing

## Implementation Hints

### Path A -- A5-A11 bypass flow:
1. Detect device in DFU mode via libusb
2. Execute checkm8 exploit (USB DMA heap corruption via ipwndfu)
3. Load custom ramdisk (HFS+ DMG with jailbreak tools)
4. Jailbreak kernel via checkra1n (nvram_unlock, mac_mount, dyld patches)
5. Unlock NVRAM for unsigned writes
6. Replace mobileactivationd with patched version
7. Generate activation record plist (FairPlay + device certs)
8. Feed to local activation daemon via lockdownd/USB
9. Write activation state to NVRAM
10. Run iOS-version-specific deletescript (Setup.app removal, cache clear)
11. Verify lock removal

### Path B -- A12+ bypass flow (drmHandshake session mode):
1. Put device in DFU mode
2. Modify device serial/identity descriptors (PWND:[checkm8] marker)
3. Query device: ECID, serial, IMEI/MEID, chip ID, iOS version
4. Determine signal vs no-signal (cellular vs WiFi-only)
5. Create session info (CreateTunnel1SessionInfoRequest)
6. Perform drmHandshake (CollectionBlob + HandshakeRequestMessage)
7. Create activation info with session response
8. Build activation record (AccountToken, FairPlayKeyData, certs)
9. Activate with session (HandleActivationInfoWithSessionRequest)
10. Run iOS-version-specific deletescript
11. Verify ActivationStateAcknowledged

### Path C -- iOS 26.2+ (DEFERRED -- see notes/future/ios_26_2_plus_bypass.md)
VU#346053 captive portal XML injection. Not in scope for v1.

### Key libraries:
- libirecovery-1.0.3 (recovery mode comms, DFU identity manipulation)
- libimobiledevice-1.0.6 (device protocol, lockdownd, mobileactivation)
- libusbmuxd-2.0.6 (USB mux)
- libplist-2.0.3 (plist generation for activation records)
- ipwndfu / ipwndfu2 / ipwndfu3 (DFU exploit delivery)
- go-ios v1.0.177 (mobileactivation protocol reference)

### Ramdisk toolchain:
- img4tool (extract from IMG4 containers)
- iBoot64Patcher (patch bootloaders)
- jbinit (ramdisk creator)
- ramdiskutil (customize restore ramdisks)
- iExtractor (automate IPSW extraction)
- Firmware keys: theapplewiki.com/wiki/Firmware_Keys

### On-device paths (deletescript targets):
- /Applications/Setup.app (delete or rename)
- /private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist
- /var/root/Library/Lockdown/activation_records/
- /var/mobile/Library/mad/activation_records/
- /var/root/Library/Lockdown/data_ark.plist
- mobileactivationd binary (replace with patched version)

### Open-source references:
- ipwndfu (github.com/axi0mX/ipwndfu) -- checkm8 BootROM exploit
- checkra1n / PongoOS -- A5-A11 jailbreak + pre-boot environment
- gaster -- checkm8 in C (good reimplementation reference)
- libimobiledevice -- device communication + activation protocol
- libideviceactivation -- activation record handling
- go-ios (github.com/danielpaulus/go-ios) -- mobileactivation module
- pymobiledevice3 (github.com/doronz88/pymobiledevice3) -- Python reference
- palera1n/ramdisk -- ramdisk builder
- jbinit (github.com/tihmstar/jbinit) -- ramdisk creator
- LIBRE-HACKTIVATOR -- deletescript reference
- iOS-Hacktivation-Toolkit -- per-version mobileactivationd scripts
- Lockra1n -- untethered bypass reference (iOS 15-16.7.8)
- JGoyd/iOS-Activation-Flaw -- VU#346053 PoC
