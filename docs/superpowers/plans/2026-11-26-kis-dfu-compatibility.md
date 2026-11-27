# Experimental KIS DFU Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect the connected A16 as KIS-carried DFU (`05ac:1881`), expose transport-safe diagnostics, and optionally attempt one explicitly enabled DFU-to-Recovery transition without ever sending classic raw-DFU requests over KIS.

**Architecture:** Add an explicit transport model to `device_info_t` and keep KIS identity probing in a dedicated libirecovery-backed module. Move the Path B mode transition into a focused helper that branches by transport, accepts Recovery only after libirecovery reports `0x1280..0x1283`, and uses an injected timeout/polling interval so the state machine is testable without hardware delays. Correct Path B Recovery identity handling to use `irecv_setenv`/`irecv_saveenv` without requiring a raw USB handle or descriptor read-back.

**Tech Stack:** C99, libirecovery 1.x, libusb 1.x, libimobiledevice, Bash, GNU Make, existing C mock framework.

**Spec:** `docs/plans/2026-11-26-kis-dfu-compatibility-design.md`

## Global Constraints

- Classic DFU is physical USB PID `05ac:1227`; KIS DFU is physical USB PID `05ac:1881` with underlying boot mode `0x1227`; classic Recovery is `05ac:1280..05ac:1283`.
- Never send classic DFU control requests or checkm8 writes over KIS `05ac:1881`.
- KIS DFU-to-Recovery is experimental and must require the explicit `--experimental-kis-transition` CLI switch.
- A KIS reboot attempt must issue exactly one `irecv_reboot` request per execution.
- Recovery is accepted only when libirecovery reports an actual mode in `0x1280..0x1283`; physical PID presence alone is insufficient.
- Before Recovery is reached, do not upload firmware, restore an IPSW, erase the device, write partitions, reset NVRAM, or write the identity environment.
- Do not modify Ubuntu libraries or global GnuTLS policy; host policy changes remain opt-in and repository-owned.
- Preserve the existing iOS 26.6.2 activation-nonce barrier in reporting; do not claim a bypass merely because the pipeline reaches its later steps.
- Keep every new C source/header file under 300 lines; do not grow touched legacy files, and reduce them where a focused extraction naturally permits. Preserve C99 compatibility.

## Review Focus

1. **Wrong transport:** A KIS device must never reach raw DFU/checkm8 code, even when `--force-path-a` is supplied.
2. **Wrong device:** A KIS reboot request with no exact ECID must fail before touching the device rather than opening an arbitrary recovery device.
3. **False Recovery:** `0x1227`, `0x1881`, an open failure, or an absent mode must not satisfy the Recovery wait.
4. **Unsupported transition:** One failed/unsupported KIS reboot request must fall back to physical Recovery guidance, wait for the configured timeout, and return a transport-specific error without a false success.
5. **USB permissions:** A non-writable Apple USB node must produce actionable udev remediation rather than a crash, hang, or unexplained “no device” result.

---

## File Map

### New production files

- `include/device/device_transport.h` — transport enum, PID classification, transport names, and Recovery-mode predicate.
- `src/device/device_transport.c` — pure transport helpers with no hardware ownership.
- `include/device/kis_dfu.h` — KIS identity probe and one-shot reboot request interfaces.
- `src/device/kis_dfu.c` — libirecovery-backed KIS probing; owns no persistent USB handle.
- `include/bypass/path_b_transition.h` — transport-aware Path B transition interface.
- `src/bypass/path_b_transition.c` — classic DFU abort, KIS one-shot reboot, and Recovery polling.

### New test/support files

- `tests/test_device_transport.c` — hardware-independent PID/mode classification tests.
- `tests/integration/test_kis_transport.c` — mock-backed KIS detection, path selection, and transition-state-machine tests.
- `tests/test_start_helpers.sh` — wrapper PID recognition, KIS parsing guard, and udev-rule checks.
- `contrib/udev/99-tr4mpass.rules` — repository-owned permissions for Apple DFU, Recovery, and KIS nodes.

### Modified production files

- `include/device/device.h` — append transport metadata and the experimental opt-in to `device_info_t`.
- `include/device/usb_dfu.h` — add KIS and Recovery PID constants.
- `src/device/device.c` — print transport, physical PID, and underlying mode.
- `src/device/usb_dfu.c` — use shared PID classification for classic DFU discovery.
- `src/main.c` — detect KIS after classic DFU, add the CLI switch, and reject forced Path A over KIS.
- `src/bypass/path_a.c` — require classic DFU transport.
- `src/bypass/path_b.c` — delegate the mode transition to the transport-aware helper.
- `src/bypass/path_b_identity.c` — support Recovery identity writes without a raw USB handle or descriptor read-back.
- `start.sh` — preserve the explicit KIS opt-in and guide the user to the repository-owned udev rule.
- `start-helpers.sh` — recognize `05ac:1881`, avoid classic-DFU descriptor parsing for KIS, and diagnose USB permissions.
- `Makefile` — include the new pure unit source and shell regression test.
- `README.md` — document KIS detection, the experimental switch, permission setup, and the iOS 26.6.2 limitation.

### Modified test files

- `tests/test_framework.h`, `tests/test_main.c`, `tests/mock_section_stubs.c` — register the new test sections.
- `tests/mocks/mock_irecv_internal.h`, `tests/mocks/mock_irecv_ctl.c`, `tests/mocks/mock_irecv_sym.c` — model physical PID separately from underlying mode; stage reboot/saveenv errors and mode sequences.
- `tests/mocks/mock_control.h` — expose the new mock controls.
- `tests/integration/test_path_b_e2e.c` — exercise Recovery identity manipulation with `dev.usb == NULL`.

---

### Task 1: Transport Model and Device Metadata

**Status:** COMPLETE — RED observed; unit 74/74, mock 264/264, production build passed.

**Files:**
- Create: `include/device/device_transport.h`
- Create: `src/device/device_transport.c`
- Create: `tests/test_device_transport.c`
- Modify: `include/device/device.h`
- Modify: `src/device/device.c`
- Modify: `Makefile`
- Modify: `tests/test_framework.h`
- Modify: `tests/test_main.c`
- Modify: `tests/mock_section_stubs.c`

**Interfaces:**
- Produces: `device_transport_t` with `DEVICE_TRANSPORT_NORMAL`, `DEVICE_TRANSPORT_CLASSIC_DFU`, `DEVICE_TRANSPORT_KIS_DFU`, and `DEVICE_TRANSPORT_RECOVERY`.
- Produces: `device_transport_t device_transport_from_usb_pid(uint16_t pid);`
- Produces: `const char *device_transport_name(device_transport_t transport);`
- Produces: `int device_transport_is_recovery_mode(int mode);`
- Produces: `int device_transport_allows_path_a(device_transport_t transport);`
- Produces: appended `device_info_t` fields `transport`, `usb_product_id`, `boot_mode`, and `experimental_kis_transition`.

- [ ] **Step 1: Add the failing transport-classification test section**

Create `tests/test_device_transport.c` with `run_device_transport_tests(void)` and assertions equivalent to:

```c
ASSERT_EQ(device_transport_from_usb_pid(0x1227), DEVICE_TRANSPORT_CLASSIC_DFU);
ASSERT_EQ(device_transport_from_usb_pid(0x1881), DEVICE_TRANSPORT_KIS_DFU);
ASSERT_EQ(device_transport_from_usb_pid(0x1281), DEVICE_TRANSPORT_RECOVERY);
ASSERT_EQ(device_transport_from_usb_pid(0x1283), DEVICE_TRANSPORT_RECOVERY);
ASSERT_EQ(device_transport_from_usb_pid(0x1234), DEVICE_TRANSPORT_NORMAL);
ASSERT_EQ(device_transport_is_recovery_mode(0x1280), 1);
ASSERT_EQ(device_transport_is_recovery_mode(0x1283), 1);
ASSERT_EQ(device_transport_is_recovery_mode(0x1227), 0);
ASSERT_EQ(device_transport_is_recovery_mode(0x1881), 0);
ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_CLASSIC_DFU), 1);
ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_KIS_DFU), 0);
ASSERT_STREQ(device_transport_name(DEVICE_TRANSPORT_KIS_DFU), "KIS DFU (Debug USB)");
```

Register the section in `tests/test_framework.h`, `tests/test_main.c`, and `tests/mock_section_stubs.c`; add `src/device/device_transport.c` to `TEST_UNITS` in `Makefile`.

- [ ] **Step 2: Run the unit target and verify it fails**

Run: `make test`

Expected: compilation fails because `device_transport.h` and the transport helper functions do not exist.

- [ ] **Step 3: Implement the transport helpers and append device metadata**

Create the enum and exact function signatures above. Implement PID mapping so only `0x1227` is classic DFU, only `0x1881` is KIS DFU, and `0x1280..0x1283` are Recovery.

Append these fields to the end of `device_info_t` in `include/device/device.h` after including `device/device_transport.h`:

```c
device_transport_t transport;
uint16_t usb_product_id;
uint16_t boot_mode;
int experimental_kis_transition;
```

Update `device_print_info()` in `src/device/device.c` to print `Transport`, `Physical USB PID`, and `Underlying Boot Mode` without changing existing parsed labels such as `CPID:` and `ECID:`. Format the ECID as 16 uppercase hexadecimal digits so the target remains visible as `0x000A7DE22260201E`.

- [ ] **Step 4: Run the unit target and verify it passes**

Run: `make test`

Expected: `Results: ... 0 failed` and all transport assertions pass.

- [ ] **Step 5: Commit the transport model**

```bash
git add include/device/device_transport.h src/device/device_transport.c \
        include/device/device.h src/device/device.c Makefile \
        tests/test_device_transport.c tests/test_framework.h \
        tests/test_main.c tests/mock_section_stubs.c
git commit -m "feat(device): add explicit USB transport model"
```

---

### Task 2: KIS Identity Detection

**Status:** COMPLETE — RED observed for detection and one-shot reboot; unit 74/74, mock 298/298, production build passed.

**Files:**
- Create: `include/device/kis_dfu.h`
- Create: `src/device/kis_dfu.c`
- Modify: `include/device/usb_dfu.h`
- Modify: `src/device/usb_dfu.c`
- Modify: `src/main.c`
- Modify: `tests/mocks/mock_irecv_internal.h`
- Modify: `tests/mocks/mock_irecv_ctl.c`
- Modify: `tests/mocks/mock_irecv_sym.c`
- Modify: `tests/mocks/mock_control.h`
- Create: `tests/integration/test_kis_transport.c`
- Modify: `tests/test_framework.h`
- Modify: `tests/test_main.c`
- Modify: `tests/mock_section_stubs.c`

**Interfaces:**
- Consumes: `device_transport_t` and `device_info_t` from Task 1.
- Produces: `int kis_dfu_detect(device_info_t *dev);`
- Produces: `int kis_dfu_request_reboot(device_info_t *dev);`
- Produces mock control: `void mock_irecv_set_device_identity(uint16_t physical_pid, int mode, uint32_t cpid, uint64_t ecid, const char *serial_string);`

- [ ] **Step 1: Extend the irecovery mock so physical PID and underlying mode are independent**

Add independent `g_irecv_info_pid`, `g_irecv_info_mode`, `g_irecv_info_cpid`, `g_irecv_info_ecid`, and serial state. Make `irecv_get_mode()` return `g_irecv_info_mode`; keep `irecv_device_info.pid` equal to the physical PID. Add `mock_irecv_set_device_identity(...)` while preserving `mock_irecv_set_device_info(...)` as a compatibility wrapper.

- [ ] **Step 2: Write failing KIS detection tests**

In `tests/integration/test_kis_transport.c`, add `run_kis_transport_tests(void)` with these cases:

1. Physical `0x1881`, underlying mode `0x1227`, CPID `0x8120`, ECID `0x000A7DE22260201E`: `kis_dfu_detect()` returns `0`, transport is KIS, physical PID is `0x1881`, mode is `0x1227`, identity fields are copied, `is_dfu_mode == 1`, and `dev.usb == NULL`.
2. Physical `0x1281` or underlying mode `0x1281`: `kis_dfu_detect()` returns `-1` and does not classify the device as KIS.
3. Staged irecovery open failure: `kis_dfu_detect()` returns `-1` without partial identity state.

Register this mock-only section and its `make test` stub.

- [ ] **Step 3: Run the mock target and verify the new tests fail**

Run: `make test-mocks`

Expected: compilation fails because `kis_dfu_detect()` is not defined.

- [ ] **Step 4: Implement the KIS probe**

Add `KIS_PID 0x1881` and Recovery range constants to `include/device/usb_dfu.h`. Implement `kis_dfu_detect()` as follows:

1. Open with `irecv_open_with_ecid_and_attempts(&client, 0, 3)` because the ECID is not known before the KIS identity block is read.
2. Read `irecv_get_mode()` and `irecv_get_device_info()`.
3. Accept only physical PID `0x1881` and underlying mode `0x1227`.
4. Copy CPID, ECID, and `serial_string` into `device_info_t`.
5. Set `is_dfu_mode = 1`, `transport = DEVICE_TRANSPORT_KIS_DFU`, `usb_product_id = 0x1881`, `boot_mode = 0x1227`, and leave `usb = NULL`.
6. Close the irecovery client on every exit path.

Implement `kis_dfu_request_reboot()` in the same module with these exact safeguards:

1. Reject a zero ECID before opening any device.
2. Open with `irecv_open_with_ecid_and_attempts(&client, dev->ecid, 3)` so only the previously identified phone is eligible.
3. Re-verify physical PID `0x1881` and underlying mode `0x1227` from the opened client.
4. Call `irecv_reboot(client)` exactly once.
5. Close the client on every exit path and return the irecovery status as success/failure.

Use the shared PID helper in `src/device/usb_dfu.c` when recognizing classic `0x1227`; do not broaden `usb_dfu_find()` to open or claim `0x1881`.

- [ ] **Step 5: Integrate KIS detection into the CLI detection order**

In `src/main.c`, after classic `usb_dfu_find()` fails and before normal-mode lockdown detection, call `kis_dfu_detect()`. On classic DFU success, set `transport = DEVICE_TRANSPORT_CLASSIC_DFU`, `usb_product_id = 0x1227`, and `boot_mode = 0x1227`.

- [ ] **Step 6: Run the mock target and verify it passes**

Run: `make test-mocks`

Expected: `Results: ... 0 failed`; the KIS fixture reports A16 identity and no raw USB handle.

- [ ] **Step 7: Commit KIS detection**

```bash
git add include/device/kis_dfu.h src/device/kis_dfu.c \
        include/device/usb_dfu.h src/device/usb_dfu.c src/main.c \
        tests/mocks/mock_irecv_internal.h tests/mocks/mock_irecv_ctl.c \
        tests/mocks/mock_irecv_sym.c tests/mocks/mock_control.h \
        tests/integration/test_kis_transport.c tests/test_framework.h \
        tests/test_main.c tests/mock_section_stubs.c
git commit -m "feat(device): detect KIS-carried DFU identity"
```

---

### Task 3: Transport-Safe Path Selection and Explicit Opt-In

**Status:** COMPLETE — RED observed for KIS Path A, forced Path A, and CLI opt-in; unit 74/74, mock 308/308, production build passed.

**Files:**
- Modify: `src/main.c`
- Modify: `src/bypass/path_a.c`
- Modify: `src/bypass/path_b.c`
- Modify: `tests/integration/test_kis_transport.c`

**Interfaces:**
- Consumes: `device_info_t.transport` and `device_info_t.experimental_kis_transition`.
- Produces CLI option: `--experimental-kis-transition` with no short alias.
- Produces behavior: forced or auto-selected Path A rejects `DEVICE_TRANSPORT_KIS_DFU`; Path B accepts an A12+ KIS device.

- [ ] **Step 1: Write failing path-selection tests**

Add mock-backed cases to `run_kis_transport_tests()`:

1. KIS A16 (`checkm8_vulnerable == 0`): `path_a_module.probe(&dev) == 0` and `path_b_module.probe(&dev) == 1`.
2. KIS A11 (`checkm8_vulnerable == 1`): `path_a_module.probe(&dev) == 0`; Path B remains `0`.
3. Classic DFU A11: `path_a_module.probe(&dev) == 1`.
4. `device_transport_allows_path_a(DEVICE_TRANSPORT_KIS_DFU) == 0`, and `main.c` uses that helper to reject Path A for KIS even when the user forces `--force-path-a`.

- [ ] **Step 2: Run the mock target and verify the path-safety assertions fail**

Run: `make test-mocks`

Expected: the KIS A11 case or forced-Path-A policy assertion fails.

- [ ] **Step 3: Enforce transport-safe path selection**

In `src/bypass/path_a.c`, require `dev->transport == DEVICE_TRANSPORT_CLASSIC_DFU` in addition to `is_dfu_mode == 1`.

In `src/main.c`, add the long option, set `dev.experimental_kis_transition = 1` only when parsed, and reject `--force-path-a` for KIS before module execution. Add the option to `--help` with wording that it enables one experimental KIS reboot request.

In `src/bypass/path_b.c`, keep Path B eligible for both classic DFU and KIS DFU, but log the transport distinctly.

- [ ] **Step 4: Run the mock target and verify it passes**

Run: `make test-mocks`

Expected: `Results: ... 0 failed`; no KIS fixture selects Path A.

- [ ] **Step 5: Commit path-selection safety**

```bash
git add src/main.c src/bypass/path_a.c src/bypass/path_b.c \
        tests/integration/test_kis_transport.c
git commit -m "fix(bypass): keep KIS devices off raw DFU path A"
```

---

### Task 4: Transport-Aware DFU-to-Recovery State Machine

**Status:** COMPLETE — RED observed for the missing transition helper; unit 74/74, mock 333/333, production build passed.

**Files:**
- Create: `include/bypass/path_b_transition.h`
- Create: `src/bypass/path_b_transition.c`
- Modify: `src/bypass/path_b.c`
- Modify: `tests/mocks/mock_irecv_internal.h`
- Modify: `tests/mocks/mock_irecv_ctl.c`
- Modify: `tests/mocks/mock_irecv_sym.c`
- Modify: `tests/mocks/mock_control.h`
- Modify: `tests/integration/test_kis_transport.c`

**Interfaces:**
- Consumes: `kis_dfu_request_reboot(device_info_t *)` from Task 2.
- Produces: `int path_b_transition_to_recovery(device_info_t *dev, unsigned int timeout_secs, unsigned int poll_secs);`
- Produces mock controls: `void mock_irecv_set_reboot_error(int error_code);` and `void mock_irecv_set_mode_sequence(const int *modes, size_t count);`

- [ ] **Step 1: Extend the irecovery mock for reboot failures and mode sequences**

Add a staged reboot error and a FIFO mode sequence consumed by successive `irecv_get_mode()` calls. When the sequence is exhausted, return the last configured mode.

- [ ] **Step 2: Write failing transition-state-machine tests**

Add these cases to `run_kis_transport_tests()`:

1. KIS without `experimental_kis_transition`: transition returns `-1`; call log contains zero `irecv_reboot` entries.
2. KIS with the opt-in but ECID `0`: transition returns `-1`; call log contains zero `irecv_reboot` entries and no arbitrary-device open.
3. KIS with opt-in and modes `[0x1227, 0x1281]`: transition returns `0`; call log contains exactly one `irecv_reboot`; final transport is Recovery and boot mode is `0x1281`.
4. KIS with staged reboot error and modes `[0x1227, 0x1281]`: transition still waits and returns `0`; exactly one reboot request is logged.
5. KIS with mode `0x1227` throughout and a short timeout: transition returns `-1`; no Recovery success is reported.
6. KIS transition: call log contains no `libusb_control_transfer` entry.
7. Classic DFU transition: call log contains one DFU abort control transfer and zero `irecv_reboot` entries.

Use `timeout_secs = 3` and `poll_secs = 0` in mock tests so the suite remains fast.

- [ ] **Step 3: Run the mock target and verify the transition tests fail**

Run: `make test-mocks`

Expected: compilation fails because `path_b_transition_to_recovery()` is not defined.

- [ ] **Step 4: Implement the transition helper**

Create `path_b_transition_to_recovery()` with these exact branches:

- **KIS:** require the opt-in; require a nonzero ECID; call `kis_dfu_request_reboot()` exactly once; close the client before polling; if the request fails, print the current Apple Face ID Recovery fallback and continue polling: quickly press/release Volume Up, quickly press/release Volume Down, then hold Side until the “Connect to computer” screen appears.
- **Classic DFU:** require `dev->usb`; send the existing DFU abort control request; close and clear the raw USB handle; then poll.
- **Recovery polling:** repeatedly open with the exact ECID through libirecovery, read `irecv_get_mode()`, and accept only `0x1280..0x1283`. On success, set transport to Recovery, set physical PID and boot mode to the accepted mode, clear `is_dfu_mode`, and return `0`. On timeout, return `-1` with a transport-specific error.

Replace the old static transition body in `src/bypass/path_b.c` with a call using `120`-second/`2`-second parameters for KIS and `60`-second/`2`-second parameters for classic DFU.

- [ ] **Step 5: Run the mock target and verify it passes**

Run: `make test-mocks`

Expected: `Results: ... 0 failed`; KIS sends exactly one reboot request and never a raw control transfer.

- [ ] **Step 6: Commit the transition helper**

```bash
git add include/bypass/path_b_transition.h src/bypass/path_b_transition.c \
        src/bypass/path_b.c tests/mocks/mock_irecv_internal.h \
        tests/mocks/mock_irecv_ctl.c tests/mocks/mock_irecv_sym.c \
        tests/mocks/mock_control.h tests/integration/test_kis_transport.c
git commit -m "feat(bypass): add transport-aware recovery transition"
```

---

### Task 5: Correct Recovery Identity Manipulation

**Status:** COMPLETE — RED observed at the stale USB-handle guard and descriptor read-back; unit 74/74, mock 338/338, production build passed.

**Files:**
- Modify: `src/bypass/path_b_identity.c`
- Modify: `tests/integration/test_path_b_e2e.c`
- Modify: `tests/mocks/mock_irecv_internal.h`
- Modify: `tests/mocks/mock_irecv_ctl.c`
- Modify: `tests/mocks/mock_irecv_sym.c`
- Modify: `tests/mocks/mock_control.h`

**Interfaces:**
- Consumes: Recovery `device_info_t` with `usb == NULL` and exact ECID.
- Produces: `path_b_manipulate_identity()` returns `0` after successful `irecv_setenv("serial-number", ...)`; `irecv_saveenv()` failure remains non-fatal but visible.
- Produces mock control: `void mock_irecv_set_saveenv_error(int error_code);`

- [ ] **Step 1: Write failing Recovery identity tests**

Update the Path B happy-path fixture so `dev.usb == NULL`, `dev.transport == DEVICE_TRANSPORT_RECOVERY`, and the mock irecovery serial string contains the original identity. Assert:

1. `path_b_manipulate_identity()` returns `0`.
2. The call log contains `irecv_setenv(serial-number=...PWND:[checkm8]...)` and `irecv_saveenv`.
3. No USB descriptor read-back is required.

Add separate cases for `irecv_setenv` failure (return `-1`) and `irecv_saveenv` failure (return `0`, with `irecv_saveenv` still called).

- [ ] **Step 2: Run the mock target and verify the Recovery identity test fails**

Run: `make test-mocks`

Expected: the `dev.usb == NULL` case fails at the stale “No USB handle for DFU mode” guard.

- [ ] **Step 3: Implement the Recovery identity correction**

In `src/bypass/path_b_identity.c`:

1. Remove the requirement for `dev->usb`.
2. Keep the Recovery-mode check in `path_b_write_serial_irecovery()`.
3. Keep successful `irecv_setenv` as the write result.
4. Keep `irecv_saveenv` failure non-fatal and log it distinctly.
5. Remove the descriptor read-back verification after `setenv`; do not claim that an NVRAM environment variable is a USB string descriptor.
6. Because Path B always reaches this step after Recovery, remove the now-unused classic-DFU descriptor branch and its local UTF-16 helpers; `path_b_read_serial()` should use libirecovery exclusively. This also reduces the touched legacy file instead of growing it.

Update file comments to describe Recovery/iBoot environment manipulation rather than DFU descriptor manipulation.

- [ ] **Step 4: Run the mock target and verify it passes**

Run: `make test-mocks`

Expected: `Results: ... 0 failed`; Recovery identity succeeds without a raw USB handle.

- [ ] **Step 5: Commit the identity correction**

```bash
git add src/bypass/path_b_identity.c tests/integration/test_path_b_e2e.c \
        tests/mocks/mock_irecv_internal.h tests/mocks/mock_irecv_ctl.c \
        tests/mocks/mock_irecv_sym.c tests/mocks/mock_control.h
git commit -m "fix(path-b): write identity in Recovery without USB handle"
```

---

### Task 6: Wrapper Detection, Permissions, and Safety Gate

**Status:** COMPLETE — RED observed for PID handling, KIS parsing, permissions, opt-in gating, macOS detection, and Recovery guidance; unit 74/74 plus wrapper 24/24, mock 338/338, production build passed.

**Files:**
- Create: `tests/test_start_helpers.sh`
- Create: `contrib/udev/99-tr4mpass.rules`
- Modify: `start-helpers.sh`
- Modify: `start.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: physical PIDs `0x1227` and `0x1881` from the wrapper’s platform-specific USB listing.
- Produces shell state: `DEVICE_USB_PID` contains the detected Apple PID.
- Produces: actionable permission remediation without applying host policy automatically.

- [ ] **Step 1: Write the failing wrapper regression script**

Create executable `tests/test_start_helpers.sh` with isolated temporary stub commands for `lsusb` and the tr4mpass binary. Assert:

1. `check_dfu` recognizes `05ac:1227` and sets `DEVICE_USB_PID=0x1227`.
2. `check_dfu` recognizes `05ac:1881` and sets `DEVICE_USB_PID=0x1881`.
3. `check_dfu` rejects an unrelated Apple PID.
4. With `DEVICE_USB_PID=0x1881`, `parse_device_info` does not invoke `lsusb -v -d 05ac:1227` and parses CPID/ECID from the binary’s KIS-aware output.
5. With an overridden non-writable `APPLE_USB_NODE`, `check_usb_permissions` prints the repository rule path, `udevadm control --reload-rules`, and `udevadm trigger` remediation.
6. `contrib/udev/99-tr4mpass.rules` contains writable `plugdev` rules for `1227`, `1281`, and `1881`.

Add the script to `make test` after the C unit binary.

- [ ] **Step 2: Run the unit target and verify the wrapper assertions fail**

Run: `make test`

Expected: the script fails because `check_dfu` does not recognize `05ac:1881` and the udev rule file does not exist.

- [ ] **Step 3: Implement wrapper PID handling and KIS-safe parsing**

In `start-helpers.sh`:

1. Parse the Apple USB PID on Linux/WSL and macOS.
2. Make `check_dfu` accept only `0x1227` and `0x1881`, storing the value in `DEVICE_USB_PID`.
3. Restrict the classic `lsusb -v -d 05ac:1227` descriptor fallback to `DEVICE_USB_PID=0x1227`.
4. For `0x1881`, use the KIS-aware binary output and display `KIS DFU (Debug USB)` as the mode.
5. Add `APPLE_USB_NODE` as an optional test override and a `check_usb_permissions` diagnostic that locates the current Apple USB node, checks read/write access, and prints copy-ready commands to install `contrib/udev/99-tr4mpass.rules`, reload udev, and trigger the rule. It must not run `sudo` automatically.
6. Add a Recovery-only Face ID guide using the current Apple sequence: quickly press/release Volume Up, quickly press/release Volume Down, then hold Side until the “Connect to computer” screen appears. Keep the existing DFU-entry guide separate.

In `start.sh`, when `DEVICE_USB_PID=0x1881` and the run is not detect-only/dry-run, require `--experimental-kis-transition`; otherwise print the exact re-run command and stop before invoking the binary. Keep the flag in `run_args` unchanged.

- [ ] **Step 4: Run the unit target and verify it passes**

Run: `make test`

Expected: C unit tests and `tests/test_start_helpers.sh` all pass with zero failures.

- [ ] **Step 5: Commit wrapper and udev support**

```bash
git add tests/test_start_helpers.sh contrib/udev/99-tr4mpass.rules \
        start-helpers.sh start.sh Makefile
git commit -m "feat(cli): recognize KIS USB and guide permission setup"
```

---

### Task 7: Documentation, Full Regression, and Hardware Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/plans/2026-11-26-kis-dfu-compatibility-design.md` only if implementation revealed a verified factual correction; do not weaken accepted safety constraints.

**Interfaces:**
- Consumes: all interfaces and user-visible behavior from Tasks 1–6.
- Produces: a reproducible build/test/hardware record and an honest final activation result.

- [ ] **Step 1: Document the supported KIS workflow**

Update `README.md` with:

- `05ac:1881` is identified as KIS DFU, not classic raw DFU.
- The exact permission commands using `contrib/udev/99-tr4mpass.rules`.
- The exact experimental command: `./start.sh --experimental-kis-transition`.
- The current Apple Face ID physical Recovery fallback sequence and a link to Apple Support article 118106.
- The statement that iOS 26.6.2 remains beyond the project’s verified Path B activation ceiling.

- [ ] **Step 2: Run the complete automated verification**

Run:

```bash
make clean
make
make test
make test-mocks
```

Expected: the production build succeeds, the chip-database canary passes, and both test suites report zero failures.

- [ ] **Step 3: Apply the repository-owned udev rule with explicit host approval**

Run interactively as the user:

```bash
sudo install -m 0644 contrib/udev/99-tr4mpass.rules /etc/udev/rules.d/99-tr4mpass.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --action=add
```

Then unplug/reconnect the phone if the current node permissions do not change. Verify the Apple node is writable by the current `plugdev` user; do not change global library or GnuTLS policy.

- [ ] **Step 4: Verify KIS detection without modifying the phone**

Run:

```bash
./tr4mpass --detect-only --verbose
```

Expected for the target device:

```text
Transport: KIS DFU (Debug USB)
Physical USB PID: 0x1881
Underlying Boot Mode: 0x1227
CPID: 0x8120
ECID: 0x000A7DE22260201E
```

If detection fails, stop and report the precise permission/libirecovery error; do not attempt a transition.

- [ ] **Step 5: Verify the dry-run path performs no device write**

Run:

```bash
./tr4mpass --dry-run --force-path-b --verbose
```

Expected: Path B is selected and the program exits before any reboot, identity write, upload, or restore.

- [ ] **Step 6: Attempt the experimental transition exactly once**

Run only after Steps 3–5 pass:

```bash
./start.sh --experimental-kis-transition
```

Record:

- whether exactly one KIS reboot request was issued;
- whether the physical or logical transition reached Recovery;
- the exact error if Recovery was not reached;
- whether the identity step was reached only after confirmed Recovery.

If the one-shot request is unsupported and physical Recovery guidance is shown, follow the printed Face ID sequence while the program waits.

- [ ] **Step 7: Observe the remaining activation boundary**

If Recovery and identity manipulation succeed, allow the existing Path B pipeline to attempt normal-mode activation and record the exact failure point. Treat the documented Secure Enclave/nonce barrier as the expected iOS 26.6.2 outcome. Do not claim activation success unless the device independently reports `Activated`.

- [ ] **Step 8: Commit documentation and record the verification result**

```bash
git add README.md
git commit -m "docs: explain experimental KIS recovery workflow"
```

Update the project checkpoint with the observed hardware result, exact successful/failing command, and any remaining blocker. Do not commit generated binaries, object files, host udev files, or device-specific secrets.

---

## Final Verification Checklist

- [ ] `05ac:1881` is classified as KIS and `05ac:1227` remains classic DFU.
- [ ] KIS never reaches raw DFU/checkm8 code, including forced Path A.
- [ ] The experimental request requires `--experimental-kis-transition` and occurs at most once.
- [ ] Recovery requires an actual libirecovery recovery mode, not PID presence.
- [ ] Recovery identity manipulation works with `dev.usb == NULL` and does not require descriptor read-back.
- [ ] Unsupported transition and timeout paths fail cleanly with physical fallback guidance.
- [ ] USB permission failures provide actionable udev commands.
- [ ] `make`, `make test`, and `make test-mocks` pass.
- [ ] Hardware results are reported without claiming an iOS 26.6.2 bypass that did not occur.
