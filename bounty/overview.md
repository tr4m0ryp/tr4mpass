# Bug Bounty Registry

## Summary
- Total findings: 2
- Confirmed bugs: 1
- Likely bugs: 1
- Estimated total payout: $12,500 - $35,000
- Submitted: 0
- Last updated: 2026-04-06

## Findings

### #01: FDR-LOCAL Private Key Exposure in Firmware
- **File:** bounty/01-fdr-local-key-exposure.md
- **Severity:** High
- **Verified:** CONFIRMED BUG
- **Estimated payout:** $10,000 - $25,000
- **Submitted:** No
- **Apple response:** --
- **Key evidence:** RSA-2048 and ECDSA P-256 private keys identical across all IPSW ramdisks, extractable with `strings` command. Keys used by AMFDRCryptoCreateLocalSigned* functions. Self-signed cert with fdr-dev@group.apple.com (dev artifact in production). SHA1 signature (deprecated).
- **Risk of "by design" rejection:** Medium -- Apple may argue LOCAL keys are intended for offline restore. Counter: identical static keys across entire fleet = single point of compromise.

### #02: Sensitive Services on Activation-Locked Device
- **File:** bounty/02-sensitive-services-locked-device.md
- **Severity:** Medium-High
- **Verified:** LIKELY BUG
- **Estimated payout:** $2,500 - $10,000
- **Submitted:** No
- **Apple response:** --
- **Key evidence:** pcapd (network capture), diagnostics_relay (unauthenticated reboot), AFC (read/write personal media), syslog streaming, crash log access, recovery mode entry with nonce exposure -- all without pairing or authentication on activation-locked device. Hactivation factory flags settable and persistent via lockdownd.
- **Risk of "by design" rejection:** Medium -- AFC read access may be intentional for setup. pcapd and diagnostics reboot are harder to justify.

## Exploit Test Results

### Latest Test Cycle: 2026-04-06
- Device: iPad8,10 iOS 26.3
- USB activation attempts: ALL FAILED (activation nonce barrier)
- WiFi captive portal: JS execution achieved, HTTPS cert pinning blocks MITM
- AFC manipulation: Files written, daemons don't process on 26.3
- Recovery mode: Env vars don't affect SEP activation
- Status: **No bypass achieved**

## Pending Research

- [ ] Extract mobileactivationd from iOS 26.1 IPSW and diff against 26.3
- [ ] Analyze CVE-2026-28859 (WebKit sandbox escape) for exploitation
- [ ] Deep RE of libFDR nonce generation code path
- [ ] Fuzz mobileactivationd with crafted plist structures
- [ ] Analyze AppleKeyStoreTestUserClient in kernel for IOKit access
- [ ] Test all captive portal profile installation methods
- [ ] Research HTTP (non-HTTPS) activation endpoints
