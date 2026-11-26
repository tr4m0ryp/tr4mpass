# Explicit Recovery Resume Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicit `--resume-recovery` mode that continues Path B from a libirecovery-confirmed Recovery device with the requested exact ECID, without issuing another KIS reboot request.

**Architecture:** Add a focused libirecovery Recovery probe that opens the requested ECID and accepts only real Recovery modes. The CLI routes explicit resume requests through that probe before any DFU probe, and Path B treats confirmed Recovery as an already-completed transition while preserving the existing exact-ECID identity write and normal-mode activation stages.

**Tech Stack:** C99, libirecovery 1.x, libusb 1.x, libimobiledevice, Bash, GNU Make, existing C mock framework.

**Spec:** `docs/superpowers/specs/2026-11-26-explicit-recovery-resume-design.md`

## Global Constraints

- `--resume-recovery` is a long-only option with no short alias.
- Resume mode requires an explicit non-zero `--ecid` value.
- Recovery must be confirmed by libirecovery mode `0x1280..0x1283`; USB PID presence alone is insufficient.
- The opened device identity's ECID must exactly equal the requested ECID before any write.
- Resume mode must not call the KIS transition or send a KIS reboot request.
- Resume mode must reject `--force-path-a` and `--experimental-kis-transition`.
- Path A/checkm8 must never operate on Recovery or KIS transports.
- No firmware upload, IPSW restore, erase, partition write, or NVRAM reset.
- Preserve the expected iOS 26.6.2 Secure Enclave/activation-nonce failure boundary; never report activation without independent device confirmation.
- Keep new C source/header files under 300 lines and preserve C99 compatibility.

## Review Focus

1. **Wrong device:** A Recovery identity whose ECID differs from `--ecid` must fail before `irecv_setenv`.
2. **False Recovery:** Physical `05ac:1281` without libirecovery mode `0x1280..0x1283` must not satisfy resume detection.
3. **Conflicting intent:** Resume plus Path A or experimental KIS transition must fail before device access.
4. **Write ordering:** Identity manipulation must remain after confirmed Recovery and exact-ECID validation.
5. **Accidental reboot:** Resume must issue zero KIS-transition reboot requests; the only later reboot is Path B's Recovery-to-normal step.

---

## File Map

### New production files

- `include/device/recovery.h` — exact-ECID Recovery probe interface.
- `src/device/recovery.c` — libirecovery-backed Recovery detection and identity population.

### New test files

- `tests/integration/test_recovery_resume.c` — mock-backed Recovery probe, CLI routing, and Path B resume tests.

### Modified production files

- `src/main.c` — add `--resume-recovery`, validate conflicts, route detection through the Recovery probe, and select Path B for resume.
- `src/bypass/path_b.c` — accept confirmed Recovery transport in the Path B probe.
- `src/bypass/path_b_transition.c` — treat confirmed Recovery as an already-completed transition without any reboot request.
- `include/bypass/path_b.h` — document Recovery-resume semantics.

### Modified test files

- `tests/integration/test_cli_options.c` — exercise resume option parsing and policy.
- `tests/mocks/mock_irecv_sym.c` — report the staged device ECID independently of the requested open ECID so mismatches are testable.
- `tests/test_framework.h`, `tests/test_main.c`, `tests/mock_section_stubs.c` — register the new mock-only section.

### Modified documentation

- `README.md` — document the explicit resume command, exact-ECID requirement, and iOS 26.6.2 limitation.
- `docs/plans/2026-11-26-kis-dfu-compatibility-design.md` — append the verified hardware note that a successful KIS reboot response does not itself enter Recovery.

---

### Task 1: Exact-ECID Recovery Probe and Explicit CLI Routing

**Files:**
- Create: `include/device/recovery.h`
- Create: `src/device/recovery.c`
- Create: `tests/integration/test_recovery_resume.c`
- Modify: `src/main.c`
- Modify: `tests/integration/test_cli_options.c`
- Modify: `tests/mocks/mock_irecv_sym.c`
- Modify: `tests/test_framework.h`
- Modify: `tests/test_main.c`
- Modify: `tests/mock_section_stubs.c`

**Interfaces:**
- Produces: `int recovery_detect_exact(device_info_t *dev, uint64_t expected_ecid);`
- Produces: `cli_opts_t.resume_recovery`, set only by the long option `--resume-recovery`.
- Consumes: `device_transport_is_recovery_mode(int mode)` and `DEVICE_TRANSPORT_RECOVERY` from `device/device_transport.h`.

- [ ] **Step 1: Register the new mock-only test section**

Add `void run_recovery_resume_tests(void);` to `tests/test_framework.h`, call it from `tests/test_main.c`, and add a skipped-section stub to `tests/mock_section_stubs.c`.

- [ ] **Step 2: Write failing Recovery probe tests**

In `tests/integration/test_recovery_resume.c`, add tests with these exact behaviors:

- `recovery_detect_exact(&dev, 0)` returns `-1`, performs no `irecv_open_with_ecid` call, and leaves all identity fields zero.
- With staged identity `pid=0x1281`, `mode=0x1281`, `cpid=0x8120`, and the requested ECID, the probe returns `0`, sets `transport=DEVICE_TRANSPORT_RECOVERY`, `usb_product_id=0x1281`, `boot_mode=0x1281`, the CPID/ECID/serial, `is_dfu_mode=0`, and `usb=NULL`.
- Modes `0x1227` and `0x1881` are rejected and leave the device fields unset.
- A staged reported ECID different from `expected_ecid` is rejected before any `irecv_setenv` call.

Update `tests/mocks/mock_irecv_sym.c` so `irecv_open_with_ecid` reports `g_irecv_info_ecid` from the staged device identity rather than echoing the requested ECID.

- [ ] **Step 3: Write failing CLI policy tests**

In `tests/integration/test_cli_options.c`, add tests asserting:

- `--resume-recovery` sets `opts.resume_recovery=1` and has no short alias.
- `--resume-recovery` without `--ecid`, or with `--ecid 0`, is rejected.
- `--resume-recovery --force-path-a` is rejected.
- `--resume-recovery --experimental-kis-transition` is rejected.
- In resume mode, `detect_device()` calls the Recovery probe before `usb_dfu_find` and never probes classic DFU or KIS.

- [ ] **Step 4: Run the mock suite to verify RED**

Run: `make test-mocks`

Expected: compilation or assertion failure because `recovery_detect_exact`, `opts.resume_recovery`, and the `--resume-recovery` option do not exist yet.

- [ ] **Step 5: Implement the minimal Recovery probe**

Create `include/device/recovery.h` with:

```c
int recovery_detect_exact(device_info_t *dev, uint64_t expected_ecid);
```

Implement `src/device/recovery.c` so it:

1. rejects a null device or zero ECID before opening;
2. calls `irecv_open_with_ecid_and_attempts(&client, expected_ecid, 3)`;
3. calls `irecv_get_mode()` and accepts only `device_transport_is_recovery_mode(mode)`;
4. requires device info whose ECID exactly equals `expected_ecid`;
5. copies identity into local variables and assigns them to `dev` only after every check succeeds;
6. sets Recovery transport/PID/mode, CPID, ECID, serial, `is_dfu_mode=0`, and `usb=NULL`;
7. closes the libirecovery client on every exit path.

- [ ] **Step 6: Implement the explicit CLI routing**

In `src/main.c`:

- add `int resume_recovery;` to `cli_opts_t`;
- add the long-only `--resume-recovery` option and usage text;
- reject missing/zero ECID and the two conflicting flags in `parse_args()`;
- in `detect_device()`, when `opts->resume_recovery` is set, call `recovery_detect_exact(dev, opts->ecid_override)` and return immediately on failure;
- in `select_module()`, when resume is requested, require `dev->transport == DEVICE_TRANSPORT_RECOVERY` and select `path_b_module`;
- leave ordinary detection order unchanged.

- [ ] **Step 7: Run focused and complete verification**

Run: `make test-mocks && make test && make`

Expected: mock suite passes with the new Section 14 assertions, hardware-independent suite remains green, and the production build succeeds.

- [ ] **Step 8: Commit Task 1**

```bash
git add include/device/recovery.h src/device/recovery.c src/main.c \
        tests/integration/test_recovery_resume.c \
        tests/integration/test_cli_options.c tests/mocks/mock_irecv_sym.c \
        tests/test_framework.h tests/test_main.c tests/mock_section_stubs.c
git commit -m "feat(device): add explicit recovery resume detection"
```

---

### Task 2: Path B Resume Semantics Without Another KIS Reboot

**Files:**
- Modify: `src/bypass/path_b.c`
- Modify: `src/bypass/path_b_transition.c`
- Modify: `include/bypass/path_b.h`
- Modify: `tests/integration/test_recovery_resume.c`

**Interfaces:**
- Consumes: `recovery_detect_exact()` and `DEVICE_TRANSPORT_RECOVERY` from Task 1.
- Produces: Path B probe acceptance for confirmed Recovery transport.
- Produces: `path_b_transition_to_recovery()` returning `0` without I/O for a confirmed Recovery device.

- [ ] **Step 1: Write failing Path B resume tests**

Add tests asserting:

- `path_b_module.probe(&dev)` returns `1` for an A16-like Recovery device with `transport=DEVICE_TRANSPORT_RECOVERY`, `boot_mode=0x1281`, `is_dfu_mode=0`, and `checkm8_vulnerable=0`.
- `path_b_transition_to_recovery(&dev, 3, 0)` returns `0` for that Recovery device and records zero `irecv_reboot` and zero `libusb_control_transfer` calls.
- The same transition helper returns `-1` when Recovery transport has `boot_mode=0x1227` or ECID zero.
- Path A still rejects Recovery transport.

- [ ] **Step 2: Run the mock suite to verify RED**

Run: `make test-mocks`

Expected: Path B probe and transition assertions fail because Recovery transport is currently rejected and the transition helper only accepts classic DFU or KIS.

- [ ] **Step 3: Implement Recovery-aware Path B behavior**

In `src/bypass/path_b.c`, allow the Path B probe to accept `DEVICE_TRANSPORT_RECOVERY` when the device is not checkm8-vulnerable.

In `src/bypass/path_b_transition.c`, add a Recovery branch to `path_b_transition_to_recovery()` that:

1. requires a non-zero ECID;
2. requires `device_transport_is_recovery_mode(dev->boot_mode)`;
3. logs that Recovery is already confirmed and the transition is skipped;
4. returns `0` without calling `irecv_reboot`, libirecovery, or libusb.

Update `include/bypass/path_b.h` comments to state that Recovery transport skips only the transition stage and preserves the exact-ECID identity stage.

- [ ] **Step 4: Run complete verification**

Run: `make test-mocks && make test && make`

Expected: all suites and the production build pass; the resume transition test proves zero reboot requests.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/bypass/path_b.c src/bypass/path_b_transition.c \
        include/bypass/path_b.h tests/integration/test_recovery_resume.c
git commit -m "feat(bypass): resume path b from confirmed recovery"
```

---

### Task 3: Documentation, Regression, and Guarded Hardware Resume

**Files:**
- Modify: `README.md`
- Modify: `docs/plans/2026-11-26-kis-dfu-compatibility-design.md` only to append the verified hardware observation.

**Interfaces:**
- Consumes: the explicit CLI and Path B behavior from Tasks 1–2.
- Produces: a reproducible command and an honest device outcome without committing device logs or identifiers beyond the already-documented research target.

- [ ] **Step 1: Document explicit Recovery resume**

Add a README subsection under the KIS workflow containing:

```bash
./tr4mpass --resume-recovery --ecid <ECID> --dry-run --verbose
./tr4mpass --resume-recovery --ecid <ECID> --verbose
```

State that `--ecid` is mandatory, Recovery must be confirmed by libirecovery, Path B skips only the already-completed transition, and the command never issues the experimental KIS reboot request. Retain the iOS 26.6.2 activation-ceiling warning.

Append to the original design's hardware-verification section that `irecv_reboot` returned success in KIS mode but the physical PID remained `05ac:1881`; physical Recovery was required.

- [ ] **Step 2: Run complete automated verification**

Run:

```bash
make clean
make
make test
make test-mocks
git diff --check
```

Expected: production build and chip-database canary pass; unit/wrapper and mock/integration suites report zero failures.

- [ ] **Step 3: Verify the live Recovery preconditions**

Before the dry run, confirm:

- `lsusb` reports `05ac:1281`;
- the current USB node is writable by the user;
- a read-only libirecovery probe reports mode `0x1281` and the requested ECID;
- on WSL, `usbipd list` shows the Recovery device attached, and the attachment uses `--auto-attach` for its current bus ID so the Recovery-to-normal reconnect can reappear without a second user intervention.

If any precondition fails, stop and report it without running the write path.

- [ ] **Step 4: Run the explicit dry run**

Run:

```bash
./tr4mpass --resume-recovery --ecid 0x000A7DE22260201E --dry-run --verbose
```

Expected: Recovery transport and exact identity are reported, Path B is selected, and the process exits before `irecv_setenv`, `irecv_saveenv`, or any reboot.

- [ ] **Step 5: Run the explicit resume once**

Run:

```bash
./tr4mpass --resume-recovery --ecid 0x000A7DE22260201E --verbose
```

Record the exact stages reached:

- Recovery confirmation;
- identity read and `setenv serial-number` result;
- `saveenv` result;
- Recovery-to-normal reboot and reconnection result;
- session handshake/activation failure point;
- independent activation state.

Expected on iOS 26.6.2: identity may succeed, but the final activation reaches and reports the known Secure Enclave/nonce barrier. Do not claim activation unless the device independently reports `Activated`.

- [ ] **Step 6: Commit documentation and record the result**

```bash
git add README.md
git commit -m "docs: explain explicit recovery resume workflow"
```

Update the execution ledger and persistent project checkpoint with the successful/failing commands and remaining blocker. Do not commit generated binaries, object files, host udev files, or device logs.

- [ ] **Step 7: Run the branch-wide verification**

Run:

```bash
make clean
make
make test
make test-mocks
git diff --check
git status --short --branch
```

Expected: all automated checks pass and only intended source/documentation changes remain.
