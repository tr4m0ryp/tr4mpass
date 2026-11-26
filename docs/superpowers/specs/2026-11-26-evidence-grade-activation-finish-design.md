# Evidence-Grade Activation Finish Design

**Status:** Proposed — awaiting user approval  
**Date:** 2026-11-26  
**Selected approach:** A — evidence-grade finish

## Context

The compatible KIS/Recovery workflow has already completed the device-side part of Path B on the attached A16:

- Recovery identity manipulation wrote and saved the `PWND` marker.
- The device returned to normal mode and was independently confirmed by `mobileactivationd` as `Unactivated`.
- A real Albert `drmHandshake` request succeeded with HTTP 200.
- The subsequent `deviceActivation` request also returned HTTP 200, but its response content type was `application/x-buddyml`—Apple's owner-credential form—not an activation-record plist.
- One earlier `mobileactivationd` service-start failure was transient; an independent probe immediately afterward succeeded.

The current implementation discards Albert's response content type and therefore reports the BuddyML response as the misleading error `Failed to parse deviceActivation response`. It also treats a transient post-reboot service-start failure as terminal. These behaviors obscure the real result and prevent a reproducible, evidence-grade conclusion.

## Goal

Complete the tooling so it:

1. waits, within a bounded interval, for `mobileactivationd` to become ready after a reboot;
2. distinguishes Apple's owner-credential response from a valid activation record and from protocol/transport failures;
3. reports the exact reachable outcome without claiming that Activation Lock was bypassed;
4. records the live A16/iOS 26.6.2 evidence and finishes the branch with automated tests, a clean build, and reproducible diagnostics.

## Non-goals and safety constraints

- Do not collect, request, store, or submit Apple ID credentials.
- Do not add an interactive owner-credential flow; that was explicitly not selected.
- Do not claim that the attached device is activated unless `mobileactivationd` independently reports `Activated`.
- Do not repeat the Recovery identity write or NVRAM save merely to validate these changes.
- Do not upload firmware, restore an IPSW, erase the device, write partitions, reset NVRAM, or alter the working KIS/Recovery transport behavior.
- Do not change the system-wide GnuTLS policy. Live online validation may continue to use the existing user-local `GNUTLS_SYSTEM_PRIORITY_FILE` under `~/.local/opt`.

## Design

### 1. Bounded post-reboot readiness probe

Add a public readiness API to the activation layer:

```c
int activation_wait_ready(device_info_t *dev, unsigned max_attempts);
```

The implementation will:

1. validate the device and lockdown handle;
2. attempt to start `com.apple.mobileactivationd`;
3. query the activation state through that service;
4. return success as soon as the state query succeeds;
5. otherwise close the client, wait two seconds, and retry up to the caller-provided limit;
6. return failure after the limit with the last service error and a clear summary.

Production callers will use 30 attempts (approximately 58 seconds of retry delay). Test builds use no wall-clock delay so retry behavior remains deterministic and fast.

`session_get_info()` will call this readiness probe before requesting a fresh session blob. This centralizes the behavior for Path B, `--activate-only`, and `--probe-albert`, so every session-based caller receives the same protection against a post-reboot service race.

The retry applies only to service startup/readiness. It does not retry an explicit device command refusal such as an unknown request, and it does not repeat activation-record submission.

### 2. Preserve and classify Albert response metadata

Extend the private HTTP response object in `src/activation/session_online.c` to retain:

- HTTP status;
- a copied, normalized response content type;
- the response body.

The content type must be copied before the curl handle is cleaned up. Matching will ignore case and optional parameters after `;`.

Add an explicit online-activation result type in `include/activation/session.h`:

```c
typedef enum {
    SESSION_ONLINE_ERROR          = -1,
    SESSION_ONLINE_SUCCESS        = 0,
    SESSION_ONLINE_ACTIVATION_LOCK = 1
} session_online_result_t;
```

`session_device_activation_online()` will return that result while preserving its existing plist output contract:

- `SESSION_ONLINE_SUCCESS`: the body parsed as an activation-record plist; the caller may submit it.
- `SESSION_ONLINE_ACTIVATION_LOCK`: the response is `application/x-buddyml`; no activation record is produced or submitted.
- `SESSION_ONLINE_ERROR`: HTTP, transport, or response-parse failure; no activation record is produced.

For forward compatibility, a non-BuddyML response is still parsed as a plist and accepted if valid even if Apple changes its content type. A malformed expected response remains an error and reports the HTTP status and content type without dumping the full body.

No BuddyML fields, credentials, or hidden form data will be parsed or retained.

### 3. Accurate user-facing outcome

`--activate-only` will handle the new result explicitly:

- success proceeds to record submission and independent activation-state verification;
- owner lock prints that Apple returned an owner-credential form, the device remains unactivated, original-owner credentials would be required, and no activation record was submitted;
- all other errors retain a generic failure exit.

Exit status remains binary: `0` only for a verified successful activation; `1` for owner lock, protocol failure, transport failure, or a state that is still not activated.

The generic `Bypass completed successfully` banner must not be used for an owner-lock response or for a device that remains `Unactivated`.

### 4. Test seams and RED→GREEN coverage

Extend the existing mocks rather than adding a new dependency:

- curl staging gains a response content type while preserving the existing response-body helper;
- `curl_easy_getinfo(CURLINFO_CONTENT_TYPE)` returns the staged type;
- mobileactivation staging gains bounded service-start/readiness failures.

Add focused automated coverage for:

1. readiness succeeds after transient startup failures and performs the expected number of attempts;
2. readiness stops at its configured attempt limit and returns failure;
3. a valid Albert plist response is parsed as success;
4. `application/x-buddyml` is classified as `SESSION_ONLINE_ACTIVATION_LOCK`, produces no activation record, and triggers no activation submission;
5. a malformed expected activation response returns `SESSION_ONLINE_ERROR`;
6. a non-200 Albert response returns `SESSION_ONLINE_ERROR` with status information;
7. no owner-lock result is ever treated as successful activation.

The implementation must be developed with strict RED→GREEN TDD: add each failing test and observe the expected failure before changing production code.

### 5. Documentation and live evidence

Update the user-facing documentation to state:

- post-reboot `mobileactivationd` readiness is retried for a bounded interval;
- Albert's BuddyML response means the original owner's credentials are required and is not a bypass;
- iOS 26.6.2 remains beyond the project's verified Path B activation ceiling, and the known Secure Enclave/FairPlay nonce barrier remains a separate constraint.

Append the attached A16 result to `26.3-vulnerability.md`, including:

- ECID/CPID/iOS identity;
- independent pre-run and post-run `Unactivated` state;
- successful `drmHandshake` status;
- `deviceActivation` status and `application/x-buddyml` content type;
- the resulting classified outcome and nonzero exit.

The evidence will distinguish the **currently reachable owner-credential boundary** from the **known cryptographic activation-nonce boundary**. It will not claim that the latter was newly crossed on this device if the owner-credential form prevents reaching it.

## Hardware validation sequence

After automated tests and the production build pass:

1. Confirm the phone is in normal mode and independently query its activation state.
2. Run detection only; do not enter Recovery and do not write identity data again.
3. Run `--activate-only --verbose` with the existing user-local GnuTLS priority file.
4. Verify the log shows bounded readiness success, successful `drmHandshake`, `deviceActivation` HTTP 200, `application/x-buddyml`, and the explicit owner-lock result.
5. Independently query activation state again and record that it is still `Unactivated`.
6. Confirm the process exits nonzero and that no activation record was submitted.

No credential form will be completed and no further destructive or firmware operation will be attempted.

## Expected files

- `include/activation/activation.h`
- `include/activation/session.h`
- `src/activation/readiness.c`
- `src/activation/session.c`
- `src/activation/session_online.c`
- `src/main.c`
- `tests/mocks/mock_afc_mobileactivation.c`
- `tests/mocks/mock_afc_mobileactivation_ctl.c`
- `tests/mocks/mock_control.h`
- `tests/mocks/mock_libcurl.c`
- a focused session/readiness test source
- `README.md`
- `26.3-vulnerability.md`

The KIS, Recovery, Path A, Path B identity, and CLI transport-selection behavior remains unchanged unless a regression test requires a compatibility-only adjustment.

## Completion criteria

The work is complete when:

- the new tests first fail for the intended reason and then pass;
- the full unit, mock/integration, and wrapper suites pass;
- the production build succeeds and `git diff --check` is clean;
- the live `--activate-only` run reports the owner-credential form accurately, exits nonzero, and is independently confirmed to leave the device `Unactivated`;
- documentation states the exact result without claiming a successful bypass.
