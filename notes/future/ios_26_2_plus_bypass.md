# iOS 26.2+ Bypass -- Future Research Path
# Status: NOT IN SCOPE FOR V1 -- research reference only
# Created: 2026-04-05

## Why Current Tools Break at iOS 26.2

Four security layers that Apple hardens with each release:

### 1. Server-mandatory activation
Apple moved the source of truth for activation to their servers at
https://humb.apple.com/humbug/baa. The device cannot self-activate
without a valid server-signed certificate. Local activation record
injection (the method used in Path A and Path B for iOS <= 26.1)
gets rejected because the device demands a server-issued signature
that we cannot forge.

### 2. MEID-based activation record signing
Starting around iOS 17+, Apple requires activation records to be
cryptographically signed with device-specific MEID data. The device
validates the signature at boot time. Unsigned or incorrectly signed
records are rejected by the bootloader before iOS even starts.

### 3. Secure Enclave (SEP) activation state storage
On A12+ devices, the activation state is stored inside the Secure
Enclave Processor. Even with full root access to iOS, the SEP is a
separate processor with its own firmware (SEPOS). The communication
protocol between iOS and SEP changes between iOS versions, breaking
handshake implementations. No public exploit exists for A12+ SEP
as of April 2026. The 2020 "unpatchable" SEP exploit only affects
A7-A11 chips.

### 4. Code signing persistence (CoreTrust/AMFI)
Patched binaries (like a modified mobileactivationd) lose their Apple
code signature. AMFI (Apple Mobile File Integrity) kills unsigned
processes. TrollStore exploited CoreTrust (CVE-2023-41991) to bypass
this, but Apple patched it in iOS 17.0.1. No replacement exists for
iOS 18+. This means any bypass that modifies on-device binaries is
tethered -- it reverts on reboot.

## Potential Bypass Vector: VU#346053 (XML Payload Injection)

### Overview
A critical vulnerability in Apple's iOS activation backend discovered
by security researcher JGoyd. Reported to Apple and US-CERT (VU#346053)
on May 19, 2025. Publicly disclosed June 26, 2025. UNPATCHED as of
April 2026.

### Technical Details
- Endpoint: https://humb.apple.com/humbug/baa
- The activation server accepts unauthenticated XML .plist payloads
- No sender verification, no signature validation on incoming payloads
- Accepts DOCTYPE declarations (opens door to XXE attacks)
- Enables arbitrary provisioning changes during device setup phase
- Persistent configuration manipulation possible

### Attack Delivery
- Captive portal: device connects to rogue WiFi AP during setup,
  HTTP responses inject payloads into activation flow
- Rogue access point with DNS spoofing
- Compromised provisioning server
- Requires NO jailbreak, NO physical device manipulation
- Works pre-activation (during Setup Assistant)

### Why This Bypasses the Four Layers
1. Server-mandatory activation: the vulnerability IS in the server
   endpoint. We inject directly into the activation pipeline, so the
   server-signing requirement becomes irrelevant.
2. MEID record signing: if provisioning changes happen at the server
   level before the device builds its activation record, the signing
   validation may not trigger.
3. SEP: if the activation endpoint returns a valid-looking response
   (because we injected into it), the SEP may accept the state as
   legitimate server-authorized activation.
4. Code signing: no on-device binary modification needed. The attack
   happens at the network/server layer.

### Open Questions for Future Implementation
- Does the injected provisioning change persist through reboot?
- Does SEP validate the activation state against the server on each
  boot, or only once during initial activation?
- Can the XXE vector be escalated to return a fully valid activation
  certificate that the device accepts permanently?
- What exact plist payload structure triggers successful activation?
- Does Apple's certificate pinning on the activation endpoint block
  MITM, or does the captive portal flow bypass it because it happens
  before TLS is established?

### References
- PoC: https://github.com/JGoyd/iOS-Activation-Flaw
- Disclosure: https://seclists.org/fulldisclosure/2025/Jun/27
- Analysis: https://gbhackers.com/apple-ios-activation-flaw-enables-injection/
- Analysis: https://cybersecuritynews.com/apples-ios-activation-vulnerability/

### Connection to Existing Work
The captive_portal/ directory in icloud_lock already contains:
- DNS server (dns_server.py)
- DHCP server (dhcp_server.py)
- HTTP server with hotspot-detect (server.py, server_v2.py)
- Beacon logger (beacon.py)
This infrastructure is directly applicable to delivering VU#346053
payloads. Future Path C implementation would extend this into Itr4m
as a C-based captive portal with XML injection capabilities.

## Implementation Plan (DEFERRED)

When we are ready to tackle iOS 26.2+:

1. Study VU#346053 PoC in detail (github.com/JGoyd/iOS-Activation-Flaw)
2. Set up test environment with iOS 26.2+ device
3. Implement C-based captive portal (DNS + HTTP)
4. Craft activation XML payload based on PoC
5. Test injection via captive portal during Setup Assistant
6. Verify persistence (reboot test)
7. If persistent: integrate as Path C in Itr4m
8. If tethered: evaluate whether re-injection on boot is feasible
9. Build iOS-version-specific payload templates
