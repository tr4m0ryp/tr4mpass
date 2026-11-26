# Explicit Recovery Resume Design

**Date:** 2026-11-26  
**Status:** Proposed

## Intent

Hardware verification showed that `irecv_reboot` can return success in KIS mode while the device remains physically at `05ac:1881`. If the user enters Recovery after the program's 120-second poll has ended, the current program exits and cannot resume because it has no standalone Recovery detection path.

Add one explicit CLI mode, `--resume-recovery`, that resumes the existing Path B pipeline only when libirecovery independently confirms an Apple device in real Recovery mode with the requested exact ECID.

## Goals

- Resume Path B from an already-confirmed Recovery device without another reboot request.
- Require the target ECID explicitly through the existing `--ecid` option.
- Confirm Recovery through libirecovery mode `0x1280..0x1283`; USB PID presence alone is insufficient.
- Preserve the existing Recovery identity, reboot-to-normal, session handshake, and activation steps.
- Keep the behavior reproducible with one direct CLI command.

## Non-goals

- No automatic Recovery resume during an ordinary run.
- No KIS reboot request in `--resume-recovery` mode.
- No raw-DFU/checkm8 operation against Recovery or KIS.
- No firmware upload, IPSW restore, erase, partition write, or NVRAM reset.
- No claim that iOS 26.6.2 passes the existing Path B activation-nonce barrier.

## Approaches considered

1. **Automatic Recovery resume:** Detect Recovery whenever present and continue Path B. This is convenient but makes a persistent identity write reachable without an explicit resume intent.
2. **Explicit `--resume-recovery` mode (chosen):** Require an explicit switch and exact ECID. This makes the potentially persistent identity operation deliberate and auditable.
3. **One-off manual helper:** Use ad-hoc libirecovery commands outside the repository. This avoids code changes but bypasses the tested orchestration and is not reproducible.

## User-visible behavior

```bash
./tr4mpass --resume-recovery --ecid 0x000A7DE22260201E --verbose
```

The command fails before any write when:

- `--ecid` is absent or zero;
- the requested ECID cannot be opened;
- libirecovery reports a mode outside `0x1280..0x1283`;
- the opened identity's ECID differs from the requested ECID;
- `--force-path-a` or `--experimental-kis-transition` is also supplied.

A dry run is available for inspection:

```bash
./tr4mpass --resume-recovery --ecid 0x000A7DE22260201E --dry-run --verbose
```

Successful detection reports transport `Recovery`, physical PID and underlying mode `0x1281`, the requested ECID, and the chip identity. Path B then skips its DFU-to-Recovery transition because Recovery is already confirmed, manipulates the identity through `irecv_setenv`/`irecv_saveenv`, reboots to normal mode, and continues the existing session-activation pipeline.

## Implementation shape

- Add a focused Recovery probe module using libirecovery exact-ECID opening and mode validation.
- Add `--resume-recovery` to the existing long-option parser with no short alias.
- In resume mode, use the Recovery probe before classic DFU, KIS DFU, and normal-mode detection.
- Mark the detected state as `DEVICE_TRANSPORT_RECOVERY`; keep `is_dfu_mode` false because Recovery is not DFU.
- Let Path B's probe accept Recovery transport.
- Let Path B execution skip only the transition step when Recovery transport is already confirmed.
- Reject Path A and KIS-transition combinations during CLI validation.

## Safety constraints

- Exact-ECID matching is required before any Recovery write.
- Recovery mode must be confirmed by libirecovery, not inferred from `lsusb` alone.
- `--resume-recovery` never calls `irecv_reboot` for the KIS transition.
- Identity manipulation remains behind the existing Path B sequence and is reached only after confirmed Recovery.
- The expected iOS 26.6.2 outcome remains an activation failure at the Secure Enclave/nonce boundary unless the device independently reports `Activated`.

## Verification

1. Observe a failing mock/unit test for missing ECID, non-Recovery mode, ECID mismatch, conflicting flags, Recovery Path B selection, and transition skipping.
2. Implement the minimum behavior and observe those tests pass.
3. Run `make clean && make && make test && make test-mocks`.
4. On the connected A16, run the dry-run command and verify no write occurs.
5. Run the explicit resume command once and record the exact identity, normal-mode, and activation outcome.
6. Document the command and the observed iOS 26.6.2 result in `README.md`.
