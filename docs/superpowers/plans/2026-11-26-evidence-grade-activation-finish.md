# Evidence-Grade Activation Finish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make tr4mpass wait for post-reboot activation-service readiness, classify Apple's owner-credential response accurately, and finish the A16/iOS 26.6.2 investigation without falsely claiming an Activation Lock bypass.

**Architecture:** Add a bounded readiness probe to the activation layer and invoke it before every fresh activation session. Extend the private Albert HTTP response with status/content-type metadata and expose an explicit online result enum for success, owner lock, and error. Keep credential handling and all destructive/firmware operations out of scope.

**Tech Stack:** C99, libimobiledevice, libplist, libcurl, project mock-library pattern, GNU Make, Linux shell validation.

**Spec:** `docs/superpowers/specs/2026-11-26-evidence-grade-activation-finish-design.md`

## Global Constraints

- Do not collect, request, store, or submit Apple ID credentials.
- Do not claim activation unless `mobileactivationd` independently reports `Activated`.
- Do not repeat the Recovery identity write or NVRAM save for validation.
- Do not upload firmware, restore an IPSW, erase the device, write partitions, reset NVRAM, or change KIS/Recovery transport behavior.
- Do not modify the system-wide GnuTLS policy; use only the existing user-local `~/.local/opt/libimobiledevice-gnutls/gnutls-allow-tls1.0.conf` for live validation.
- Production readiness delay is exactly 2 seconds between attempts; test builds use a zero delay.
- Production session readiness uses exactly 30 attempts (at most 58 seconds of delay).
- Exit status is `0` only for verified activation; owner lock, protocol error, transport error, and still-unactivated states return `1`.
- Every production behavior change follows strict RED→GREEN TDD: observe the intended failing test before implementation.

## Review Focus

- Content type with different case or parameters, such as `Application/X-BuddyML; charset=utf-8`: must still classify as owner lock.
- Missing or changed content type with a valid plist body: must remain forward-compatible and parse successfully.
- Malformed plist with an expected activation content type: must return an error and submit no record.
- Service that remains unavailable after reboot: must stop at the configured attempt limit instead of hanging or retrying forever.
- Invalid readiness inputs (`NULL` device/handle or zero attempts): must fail before any service call.
- Explicit `UNKNOWN_REQUEST` command refusal: must fail immediately rather than being mistaken for a transient readiness race.

---

### Task 0: Preserve the Approved Compatibility Documentation

**Files:**
- Modify: `README.md`
- Create: `docs/plans/2026-11-26-kis-dfu-compatibility-design.md`
- Create: `docs/superpowers/plans/2026-11-26-kis-dfu-compatibility.md`
- Create: `docs/superpowers/specs/2026-11-26-explicit-recovery-resume-design.md`
- Create: `docs/superpowers/plans/2026-11-26-explicit-recovery-resume.md`
- Create: `docs/superpowers/specs/2026-11-26-evidence-grade-activation-finish-design.md`
- Create: `docs/superpowers/plans/2026-11-26-evidence-grade-activation-finish.md`

**Interfaces:**
- Consumes: the already-approved KIS/Recovery compatibility and evidence-grade activation designs.
- Produces: one clean documentation baseline before production behavior changes.

- [ ] **Step 1: Verify the pending documentation is complete and internally consistent**

Read each of the six documents end-to-end. Confirm that the KIS design, KIS plan, Recovery-resume design, Recovery-resume plan, evidence-grade design, and evidence-grade plan agree on these exact invariants:

- KIS physical PID `0x1881` is distinct from classic DFU `0x1227`;
- `--experimental-kis-transition` is long-only and enables at most one KIS reboot request;
- `--resume-recovery` requires a nonzero exact ECID, confirms Recovery mode `0x1280..0x1283`, rejects Path A/experimental-transition conflicts, and issues no KIS reboot;
- no firmware upload, IPSW restore, erase, partition write, or NVRAM reset is authorized.

Fix any truncated sentence or contradictory statement before committing.

- [ ] **Step 2: Verify the pending diff**

Run: `git diff --check && git status --short && git diff -- README.md`

Expected: no whitespace errors; only `README.md` and the intended `docs/` files are pending.

- [ ] **Step 3: Commit the documentation baseline**

```bash
git add README.md docs
git commit -m "docs: describe KIS and recovery compatibility"
```

### Task 1: Bounded Post-Reboot Activation Readiness

**Files:**
- Create: `tests/integration/test_activation_online.c`
- Create: `src/activation/readiness.c`
- Modify: `include/activation/activation.h`
- Modify: `src/activation/session.c`
- Modify: `tests/mocks/mock_afc_mobileactivation.c`
- Modify: `tests/mocks/mock_afc_mobileactivation_ctl.c`
- Modify: `tests/mocks/mock_control.h`
- Modify: `tests/test_framework.h`
- Modify: `tests/test_main.c`
- Modify: `tests/mock_section_stubs.c`

**Interfaces:**
- Produces: `#define ACTIVATION_READY_MAX_ATTEMPTS 30u`
- Produces: `int activation_wait_ready(device_info_t *dev, unsigned max_attempts);` returning `0` when the activation state query succeeds and `-1` on invalid input or exhausted attempts.
- Produces mock controls: `void mock_mobileactivation_set_start_service_failures(int error_code, int failure_count);` and `void mock_mobileactivation_set_state_query_failures(int error_code, int failure_count);`.
- Preserves: `int session_get_info(device_info_t *dev, plist_t *session_info);`; it now calls `activation_wait_ready(dev, ACTIVATION_READY_MAX_ATTEMPTS)` before requesting the session blob.

- [ ] **Step 1: Add the mock-backed readiness test section and failing assertions**

Create `tests/integration/test_activation_online.c` with `void run_activation_online_tests(void)` and these tests:

```c
test_activation_wait_ready_retries_transient_startup_failures
test_activation_wait_ready_retries_transient_state_failures
test_activation_wait_ready_stops_at_attempt_limit
test_activation_wait_ready_does_not_retry_unknown_request
test_activation_wait_ready_rejects_invalid_inputs
test_session_get_info_waits_for_service_readiness
```

Use a fake non-NULL `dev.handle`, count `mobileactivation_client_start_service` and `mobileactivation_get_activation_state` entries in the mock call log, and assert:

- two staged startup failures followed by `activation_wait_ready(&dev, 5)` return `0`, with 3 startup calls and 1 state query;
- two staged state-query failures return `0`, with 3 startup calls and 3 state queries;
- persistent startup failure with `max_attempts=3` returns `-1`, with exactly 3 startup calls;
- one staged `MOBILEACTIVATION_E_UNKNOWN_REQUEST` state-query failure returns `-1` after exactly 1 startup and 1 state-query call;
- a NULL handle and `max_attempts=0` each return `-1`, with no service calls;
- one staged startup failure followed by `session_get_info(&dev, &session_info)` returns `0`, produces a non-NULL session plist, and records 3 startup calls (2 readiness attempts plus the session request).

Stage startup failures with `mock_mobileactivation_set_start_service_failures(MOBILEACTIVATION_E_MUX_ERROR, count)` and state failures with `mock_mobileactivation_set_state_query_failures(MOBILEACTIVATION_E_REQUEST_FAILED, count)`, except the permanent-refusal case, which uses `MOBILEACTIVATION_E_UNKNOWN_REQUEST`.

Declare `run_activation_online_tests()` in `tests/test_framework.h`, call it from `tests/test_main.c`, and add an empty stub to `tests/mock_section_stubs.c` so the hardware-independent `make test` target still links.

- [ ] **Step 2: Run the focused mock test and observe RED**

Run: `make test-mocks`

Expected: compilation fails because `activation_wait_ready`, `ACTIVATION_READY_MAX_ATTEMPTS`, and the two mock controls do not exist. Save the failure output; do not add production code before observing it.

- [ ] **Step 3: Extend the mobileactivation mock with bounded failure staging**

In `tests/mocks/mock_afc_mobileactivation_ctl.c`, add independent staged error/count pairs for failed service starts and failed state queries, reset both pairs in `mock_afc_mobileactivation_reset()`, and implement the two controls declared in `mock_control.h`.

In `tests/mocks/mock_afc_mobileactivation.c`:

- `mobileactivation_client_start_service()` returns the staged startup error, consumes one failure, and does not allocate a client;
- `mobileactivation_get_activation_state()` returns the staged state-query error, consumes one failure, and leaves `*state` NULL.

- [ ] **Step 4: Implement the bounded readiness probe**

In `include/activation/activation.h`, add `ACTIVATION_READY_MAX_ATTEMPTS` and the `activation_wait_ready()` declaration with ownership/return semantics in the comment.

In the new focused module `src/activation/readiness.c`, implement `activation_wait_ready()` with these exact rules:

1. reject a NULL device, NULL `dev->handle`, or `max_attempts == 0` before any library call;
2. on each attempt, start `com.apple.mobileactivationd` and, if startup succeeds, query the activation state;
3. free the returned state plist and client on both success and failure;
4. return `0` immediately after a successful state query;
5. retain the last library error and retry transient failures until `max_attempts` is exhausted;
6. treat `MOBILEACTIVATION_E_INVALID_ARG`, `MOBILEACTIVATION_E_PLIST_ERROR`, and `MOBILEACTIVATION_E_UNKNOWN_REQUEST` as permanent and return `-1` immediately;
7. sleep exactly 2,000,000 microseconds between transient attempts in production and zero microseconds under `UNIT_TEST`;
8. return `-1` after the final transient failure and log the attempt count plus last error.

Production code must handle every non-success startup/state error; only the three permanent errors above bypass the retry loop.

- [ ] **Step 5: Gate fresh session requests on readiness**

In `src/activation/session.c`, call `activation_wait_ready(dev, ACTIVATION_READY_MAX_ATTEMPTS)` at the start of `session_get_info()`, after argument validation and before `ma_session_start()`. Return `-1` without requesting a session blob if readiness fails.

- [ ] **Step 6: Run the focused and regression suites and observe GREEN**

Run: `make test-mocks && make test`

Expected: the new readiness tests pass; existing Path B, KIS, Recovery, unit, and wrapper tests remain green; both commands exit `0`.

- [ ] **Step 7: Commit the readiness slice**

```bash
git add include/activation/activation.h src/activation/readiness.c src/activation/session.c \
        tests/integration/test_activation_online.c tests/mocks/mock_afc_mobileactivation.c \
        tests/mocks/mock_afc_mobileactivation_ctl.c tests/mocks/mock_control.h \
        tests/test_framework.h tests/test_main.c tests/mock_section_stubs.c
git commit -m "feat(activation): retry post-reboot service readiness"
```

### Task 2: Classify Albert BuddyML and Protocol Responses

**Files:**
- Modify: `include/activation/session.h`
- Modify: `src/activation/session_online.c`
- Modify: `tests/mocks/mock_libcurl.c`
- Modify: `tests/mocks/mock_control.h`
- Modify: `tests/integration/test_activation_online.c`

**Interfaces:**
- Produces: `typedef enum { SESSION_ONLINE_ERROR = -1, SESSION_ONLINE_SUCCESS = 0, SESSION_ONLINE_ACTIVATION_LOCK = 1 } session_online_result_t;`.
- Changes: `session_online_result_t session_device_activation_online(device_info_t *dev, plist_t activation_info, plist_t *activation_record);`.
- Produces mock control: `void mock_curl_set_response_with_content_type(const char *url_substring, long http_code, const char *content_type, const void *body, size_t body_len);` while preserving `mock_curl_set_response()` unchanged for existing callers.

- [ ] **Step 1: Add failing Albert response-classification tests**

Extend `tests/integration/test_activation_online.c` with:

```c
test_albert_valid_plist_response_succeeds
test_albert_buddyml_response_reports_activation_lock
test_albert_buddyml_response_is_case_and_parameter_insensitive
test_albert_missing_content_type_with_valid_plist_succeeds
test_albert_malformed_plist_response_is_error
test_albert_non_200_response_is_error
```

Create the request plist with `plist_new_dict()`, add key `ActivationInfo` with string value `fixture`, and pass it as `activation_info`. Stage responses by URL substring (`drmHandshake` or `deviceActivation`). Use this exact valid fixture for successful cases:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>ActivationRecord</key><string>fixture</string></dict></plist>
```

Use `<html><body>owner credentials</body></html>` as the BuddyML body and `<not-a-plist>` as the malformed body. Assert:

- a valid plist body returns `SESSION_ONLINE_SUCCESS` and produces a non-NULL activation-record plist;
- `application/x-buddyml` returns `SESSION_ONLINE_ACTIVATION_LOCK`, leaves the output plist NULL, and records no `mobileactivation_activate_with_session` call;
- `Application/X-BuddyML; charset=utf-8` has the same result;
- a valid plist with no staged content type still returns success;
- malformed XML returns `SESSION_ONLINE_ERROR` with a NULL output plist;
- HTTP 500 returns `SESSION_ONLINE_ERROR` with a NULL output plist.

- [ ] **Step 2: Run the focused mock test and observe RED**

Run: `make test-mocks`

Expected: compilation fails because `session_online_result_t`, the changed return type, and `mock_curl_set_response_with_content_type()` do not exist.

- [ ] **Step 3: Extend curl response staging with content type**

In `tests/mocks/mock_control.h`, declare `mock_curl_set_response_with_content_type()` with the exact signature above.

In `tests/mocks/mock_libcurl.c`:

- add a copied content-type string to each staged response;
- implement the new setter and delegate the existing `mock_curl_set_response()` to it with a NULL content type;
- make `curl_easy_getinfo(CURLINFO_CONTENT_TYPE)` return the staged content type, or NULL when none was staged;
- preserve current URL-substring matching, body delivery, HTTP status, reset, and call-count behavior.

- [ ] **Step 4: Preserve Albert HTTP status and content-type metadata**

In `src/activation/session_online.c`, extend the private response structure with a 128-byte content-type buffer and a `long http_status` field. In `http_post()`:

1. query `CURLINFO_RESPONSE_CODE` and `CURLINFO_CONTENT_TYPE` after `curl_easy_perform()`;
2. copy the content type into the response structure before `curl_easy_cleanup()`;
3. preserve the current HTTP-200 and curl-error behavior;
4. include status and content type in error logging; never log the complete response body (the existing non-200 error preview may remain capped at 512 bytes).

Add a private case-insensitive content-type comparison that trims surrounding whitespace, ignores parameters after `;`, and compares without case sensitivity.

- [ ] **Step 5: Implement the explicit online result contract**

In `include/activation/session.h`, add `session_online_result_t` and change `session_device_activation_online()` to return it.

In `src/activation/session_online.c`:

- HTTP/transport failure returns `SESSION_ONLINE_ERROR`;
- `application/x-buddyml` returns `SESSION_ONLINE_ACTIVATION_LOCK`, logs the HTTP status, actual content type, and that Albert returned an owner-credential form, and never creates an activation-record plist;
- every other response is parsed as a plist and returns `SESSION_ONLINE_SUCCESS` when parsing succeeds, even if the content type is absent or changed;
- parse failure returns `SESSION_ONLINE_ERROR` and identifies the status/content type in the log;
- the output plist is NULL for every non-success result.

Do not parse or retain BuddyML fields, credentials, or hidden form data.

- [ ] **Step 6: Run the focused and regression suites and observe GREEN**

Run: `make test-mocks && make test`

Expected: all Albert classification tests pass; existing KIS, Recovery, Path A/Path B, unit, and wrapper tests remain green.

- [ ] **Step 7: Commit the Albert classification slice**

```bash
git add include/activation/session.h src/activation/session_online.c \
        tests/mocks/mock_libcurl.c tests/mocks/mock_control.h \
        tests/integration/test_activation_online.c
git commit -m "feat(activation): classify Albert owner-lock responses"
```

### Task 3: Make the Online Outcome Unambiguous at the CLI Boundary

**Files:**
- Modify: `include/activation/session.h`
- Modify: `src/activation/session_online.c`
- Modify: `src/main.c`
- Modify: `tests/integration/test_activation_online.c`

**Interfaces:**
- Produces: `const char *session_online_result_message(session_online_result_t result);`.
- Produces: `int session_online_result_exit_code(session_online_result_t result);`.
- Consumes: `session_online_result_t session_device_activation_online(...)` from Task 2.

- [ ] **Step 1: Add failing result-message and exit-code tests**

Add these tests to `tests/integration/test_activation_online.c`:

```c
test_session_online_owner_lock_message_is_explicit
test_session_online_exit_codes_distinguish_success
```

Assert:

- the owner-lock message contains `Activation Lock`, `owner-credential form`, `remains unactivated`, `original owner's Apple ID credentials`, and `no activation record was submitted`;
- the owner-lock message does not contain `success` or `bypassed`;
- `session_online_result_exit_code(SESSION_ONLINE_SUCCESS)` is `0`;
- owner-lock and error exit codes are both `1`.

- [ ] **Step 2: Run the focused mock test and observe RED**

Run: `make test-mocks`

Expected: compilation fails because the message and exit-code helpers do not exist.

- [ ] **Step 3: Implement deterministic result messaging**

In `include/activation/session.h`, declare both helpers with their exact return semantics.

In `src/activation/session_online.c`, return this exact owner-lock message for `SESSION_ONLINE_ACTIVATION_LOCK`:

```text
Activation Lock is still active. Albert returned an owner-credential form. The device remains unactivated; the original owner's Apple ID credentials are required, and no activation record was submitted.
```

Return a generic online-activation failure message for `SESSION_ONLINE_ERROR` and a neutral success message for `SESSION_ONLINE_SUCCESS`. Return exit code `0` only for success and `1` for both other values.

- [ ] **Step 4: Integrate the explicit outcome into `--activate-only`**

In `src/main.c`, store the result of `session_device_activation_online()` in a `session_online_result_t`. Before calling `session_activate()`:

- on `SESSION_ONLINE_ACTIVATION_LOCK`, print `session_online_result_message(result)`, set the final exit code through `session_online_result_exit_code(result)`, and skip `session_activate()`;
- on `SESSION_ONLINE_ERROR`, print the generic result message and skip `session_activate()`;
- on `SESSION_ONLINE_SUCCESS`, continue to `session_activate()` and independent state verification exactly as today.

Keep all existing plist cleanup paths intact. Do not print any success banner for owner lock or for a device still reporting `Unactivated`.

- [ ] **Step 5: Run focused, regression, and production build checks**

Run: `make test-mocks && make test && make`

Expected: all suites and the production build exit `0`; the owner-lock path compiles with no implicit enum/int mismatch.

- [ ] **Step 6: Commit the CLI outcome slice**

```bash
git add include/activation/session.h src/activation/session_online.c src/main.c \
        tests/integration/test_activation_online.c
git commit -m "fix(cli): report Albert owner lock without success"
```

### Task 4: Documentation and Live A16 Evidence

**Files:**
- Modify: `README.md`
- Modify: `26.3-vulnerability.md`
- Create: `/tmp/opencode/evidence-grade-activation-pre.log`
- Create: `/tmp/opencode/evidence-grade-activation-run.log`
- Create: `/tmp/opencode/evidence-grade-activation-post.log`

**Interfaces:**
- Consumes: the classified owner-lock result and bounded readiness behavior implemented in Tasks 1-3.
- Produces: user-facing behavior documentation and a dated, independently corroborated live result.

- [ ] **Step 1: Document the bounded readiness and owner-lock behavior**

In `README.md`, add a concise online-activation subsection stating:

- `mobileactivationd` readiness is retried up to 30 attempts at 2-second intervals after reboot;
- `application/x-buddyml` from Albert means the original owner's credentials are required and is not a bypass;
- no credential form is submitted by tr4mpass;
- the process exits nonzero while the device remains `Unactivated`;
- iOS 26.6.2 remains beyond the verified Path B activation ceiling and the known Secure Enclave/FairPlay nonce barrier is a separate constraint.

- [ ] **Step 2: Capture the independent pre-run activation state**

With the phone in normal mode, query lockdownd independently of tr4mpass and save the UDID, timestamp, state, and exit code:

```bash
set +e
{
    date -Is
    printf 'udid='; idevice_id -l
    GNUTLS_SYSTEM_PRIORITY_FILE="$HOME/.local/opt/libimobiledevice-gnutls/gnutls-allow-tls1.0.conf" \
        ideviceinfo -k ActivationState
    printf 'exit_code=%s\n' "$?"
} | tee /tmp/opencode/evidence-grade-activation-pre.log
set -e
```

Expected: one UDID and `Unactivated`. This command is read-only and does not modify device state.

- [ ] **Step 3: Run non-mutating detection**

Run: `GNUTLS_SYSTEM_PRIORITY_FILE="$HOME/.local/opt/libimobiledevice-gnutls/gnutls-allow-tls1.0.conf" ./tr4mpass --detect-only --verbose`

Expected: normal-mode device detection succeeds; the command does not enter Recovery, write identity, or issue an activation request.

- [ ] **Step 4: Run the online activation path and capture its exact result**

Run:

```bash
set +e
GNUTLS_SYSTEM_PRIORITY_FILE="$HOME/.local/opt/libimobiledevice-gnutls/gnutls-allow-tls1.0.conf" \
    ./tr4mpass --activate-only --verbose \
    > /tmp/opencode/evidence-grade-activation-run.log 2>&1
rc=$?
set -e
printf 'exit_code=%s\n' "$rc" | tee -a /tmp/opencode/evidence-grade-activation-run.log
```

Expected: readiness succeeds, `drmHandshake` succeeds, `deviceActivation` reports HTTP 200 plus `application/x-buddyml`, the owner-lock message is printed, no activation record is submitted, and `exit_code=1`.

- [ ] **Step 5: Capture the independent post-run activation state**

Repeat the Step 2 command, replacing `evidence-grade-activation-pre.log` with `evidence-grade-activation-post.log`.

Expected: the same UDID still reports `Unactivated`.

- [ ] **Step 6: Record the live evidence without overstating the result**

Append a dated A16/iOS 26.6.2 section to `26.3-vulnerability.md` containing:

- CPID `0x8120`, ECID `0x000A7DE22260201E`, iOS `26.6.2`;
- independent pre-run and post-run `Unactivated` states;
- successful Albert `drmHandshake`;
- `deviceActivation` HTTP 200 and `application/x-buddyml`;
- classified owner-lock outcome and nonzero exit;
- the statement that no activation record was submitted and no successful bypass is claimed;
- the distinction between the reachable owner-credential boundary and the separately known Secure Enclave/FairPlay nonce barrier.

Do not claim that the cryptographic barrier was newly crossed if the owner-credential form prevented reaching it.

- [ ] **Step 7: Verify the documentation diff and commit**

Run: `git diff --check && git diff -- README.md 26.3-vulnerability.md`

Expected: no whitespace errors; the documentation contains the exact live result and no success claim.

Commit:

```bash
git add README.md 26.3-vulnerability.md
git commit -m "docs: record A16 owner-lock activation evidence"
```

### Task 5: Independent Whole-Branch Review

**Files:**
- Review: `git diff origin/main...HEAD`
- Modify only files implicated by review findings.

**Interfaces:**
- Consumes: the documentation, implementation, test, and evidence commits from Tasks 0-4.
- Produces: reviewed and corrected behavior before final verification.

- [ ] **Step 1: Invoke the code-review workflow**

Invoke the `requesting-code-review` skill and request a fresh review of `origin/main...HEAD`. Include the approved spec and this plan as requirements.

- [ ] **Step 2: Review the readiness boundary**

The reviewer must verify that:

- invalid inputs fail before any service call;
- `INVALID_ARG`, `PLIST_ERROR`, and `UNKNOWN_REQUEST` are not retried;
- transient startup/state failures are bounded by the exact attempt limit;
- production delay is exactly 2 seconds and test delay is zero;
- every session caller reaches the bounded readiness probe through `session_get_info()`.

- [ ] **Step 3: Review the Albert/CLI boundary**

The reviewer must verify that:

- content-type matching ignores case, surrounding whitespace, and `;` parameters;
- only BuddyML maps to owner lock;
- valid plists remain accepted when content type is missing or changed;
- malformed/non-200 responses map to error;
- owner lock produces no activation-record submission;
- the CLI prints the exact owner-lock message and exits `1`;
- exit `0` still requires independent `Activated` state.

- [ ] **Step 4: Process findings rigorously**

Invoke `receiving-code-review` before changing code. Verify each finding against the spec and tests; reject technically incorrect or out-of-scope suggestions. For every accepted finding, add or adjust a failing regression test first, implement the minimum correction, and rerun `make test-mocks && make test && make`.

If an accepted finding changes runtime, readiness, response classification, CLI output, or exit behavior, repeat Task 4 Steps 2-6 against the corrected binary and update the live evidence before continuing.

- [ ] **Step 5: Commit accepted review fixes**

Stage only files changed for accepted findings and commit with a focused message. If there are no accepted findings, record that the review passed and make no empty commit.

### Task 6: Final Verification Gate

**Files:**
- Modify: `.superpowers/sdd/2026-11-26-kis-dfu-compatibility/progress.md` only if it exists and is the branch's active task ledger.

**Interfaces:**
- Consumes: all documentation, implementation, test, evidence, and review commits from Tasks 0-5.
- Produces: fresh verification evidence and an accurate project checkpoint.

- [ ] **Step 1: Invoke the completion-verification workflow**

Invoke the `verification-before-completion` skill before making any success or completion claim. Follow its evidence-before-assertion requirements for the commands in Step 2.

- [ ] **Step 2: Run a clean full build and all test suites**

Run:

```bash
make clean
make test
make test-mocks
make
git diff --check
```

Expected: every command exits `0`; unit, wrapper, mock/integration, and production build results are all reported without failures.

- [ ] **Step 3: Audit the final diff for forbidden behavior**

Run:

```bash
git diff origin/main...HEAD --stat
git diff origin/main...HEAD -- src include tests docs README.md 26.3-vulnerability.md
```

Expected: the diff contains only the approved KIS/Recovery compatibility work, readiness retry, response classification, explicit CLI outcome, tests, and documentation. It contains no credential prompt/storage/submission, firmware upload, IPSW restore, erase, partition write, NVRAM reset, or system-wide GnuTLS-policy change.

- [ ] **Step 4: Update the branch checkpoint**

Record the final commit, test/build counts, live owner-lock result, independent pre/post `Unactivated` evidence, and the explicit statement that no successful bypass was achieved. If `.superpowers/sdd/2026-11-26-kis-dfu-compatibility/progress.md` exists, append the checkpoint there; otherwise update persistent project memory only.

- [ ] **Step 5: Commit checkpoint-only changes, if any**

```bash
git add .superpowers/sdd/2026-11-26-kis-dfu-compatibility/progress.md
git commit -m "chore: record evidence-grade activation checkpoint"
```

Skip this commit when the checkpoint path does not exist or has no tracked changes.
