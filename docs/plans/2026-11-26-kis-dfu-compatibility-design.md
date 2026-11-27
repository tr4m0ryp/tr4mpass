# Experimental KIS DFU Compatibility Design

**Date:** 2026-11-26  
**Target:** A16 (`CPID 0x8120`), ECID `0x000A7DE22260201E`, USB `05ac:1881` (Debug USB/KIS carrying boot mode `0x1227`)  
**Selected approach:** Experimental full attempt

## Goal

Make tr4mpass detect the connected iPhone as **KIS-carried DFU mode**, identify it as an A16, attempt a narrowly scoped DFU-to-Recovery transition, and continue Path B only if Recovery is actually reached.

The implementation must distinguish:

- classic DFU: physical USB PID `05ac:1227`;
- KIS DFU: physical USB PID `05ac:1881`, underlying boot mode `0x1227`;
- classic Recovery: physical USB PID `05ac:1280..05ac:1283`.

It must never treat KIS DFU as classic raw DFU for checkm8 writes.

## Verified facts and constraints

1. `lsusb` reports `05ac:1881`.
2. libirecovery reports the underlying mode as `0x1227` and identifies CPID `0x8120`, ECID `0x000A7DE22260201E`, and iBoot `7195.0.0.300.25`.
3. The repository's raw libusb layer recognizes only physical PID `0x1227`, so it misses this valid device.
4. Upstream libirecovery supports KIS DFU identity reads and file upload, but its CLI explicitly reports that **iBoot shell commands are unavailable in KIS mode**.
5. The current Path B implementation contains two known blockers after a classic DFU-to-Recovery transition:
   - it requires `dev->usb`, even though that handle is intentionally closed before Recovery;
   - it expects a USB serial-descriptor read-back after `setenv serial-number`, although `setenv` changes an NVRAM environment variable rather than the descriptor.
6. iOS 26.6.2 is newer than the repository's stated Path B ceiling (26.1). Even a successful Recovery transition is expected to reach the known activation-nonce barrier and fail the final activation step.
7. The current USB device node is `root:root 0664`. The non-root user therefore receives `LIBUSB_ERROR_ACCESS`; a udev rule is required for repeatable host-side operation.

## Scope

### In scope

- Explicit transport-aware detection for classic DFU and KIS DFU.
- KIS identity probing through libirecovery.
- An explicit experimental CLI switch that enables one KIS reboot request.
- Transport-specific DFU-to-Recovery handling:
  - classic DFU: retain the current DFU-abort path;
  - KIS DFU: issue one experimental reboot request, then poll for Recovery.
- Interactive physical Recovery fallback if the KIS request is unsupported.
- Correct Recovery identity manipulation through `irecv_setenv`/`irecv_saveenv`.
- Accurate failure reporting that distinguishes transport failure from the later activation failure.
- A local udev rule and diagnostics for USB-node permissions.

### Out of scope

- No checkm8 raw-DFU writes against `05ac:1881`.
- No firmware upload, IPSW restore, erase, partition write, or NVRAM reset.
- No claim that Path B can bypass iOS 26.6.2.
- No global replacement of Ubuntu USB or GnuTLS libraries.
- No automatic modification of the device before Recovery is reached and the user starts the bypass.

## User-visible behavior

### Detection

`tr4mpass --detect-only --verbose` should report:

- transport: `KIS DFU (Debug USB)`;
- physical USB PID: `0x1881`;
- underlying boot mode: `0x1227`;
- chip: `A16`;
- CPID: `0x8120`;
- ECID: `0x000A7DE22260201E`.

### Experimental transition

The new switch will be explicit:

```text
--experimental-kis-transition
```

For KIS DFU it will:

1. open the exact ECID through libirecovery;
2. issue one `irecv_reboot` request;
3. close the client so the USB interface is not held;
4. poll for classic Recovery (`0x1280..0x1283`) for up to 120 seconds.

If the request is unsupported and no Recovery appears, the program will print the current Apple Face ID physical Recovery fallback and continue polling:

1. quickly press and release Volume Up;
2. quickly press and release Volume Down;
3. press and hold Side until the “Connect to computer” screen appears.

This is the Recovery sequence published by Apple Support article 118106, not the DFU-entry sequence.

No firmware is uploaded and no restore is started.

### Path B identity step

In Recovery, the program will:

1. read the iBoot identity through libirecovery;
2. append the existing `PWND:[checkm8]` marker to the serial value;
3. call `irecv_setenv(client, "serial-number", ...)`;
4. call `irecv_saveenv(client)`;
5. treat successful `setenv` as the write result and report `saveenv` failure separately.

The impossible USB-descriptor read-back verification will be removed.

## Code design

### 1. Device transport model

Extend `device_info_t` with an explicit transport enum and physical USB PID:

```c
typedef enum {
    DEVICE_TRANSPORT_NORMAL = 0,
    DEVICE_TRANSPORT_CLASSIC_DFU,
    DEVICE_TRANSPORT_KIS_DFU,
    DEVICE_TRANSPORT_RECOVERY
} device_transport_t;
```

The enum and PID are appended to `device_info_t` to preserve source-compatible struct initialization.

### 2. Detection boundary

`src/device/usb_dfu.c` will classify USB PIDs, but it will not claim or operate the KIS interface using classic DFU requests.

A new KIS-aware device probe will:

- enumerate for `05ac:1881`;
- call `irecv_open_with_ecid_and_attempts`;
- copy CPID, ECID, and serial identity from `irecv_device_info`;
- record transport `DEVICE_TRANSPORT_KIS_DFU` and physical PID `0x1881`;
- leave `dev->usb == NULL` because KIS has exclusive-interface semantics and libirecovery owns the KIS client.

Classic `0x1227` detection remains unchanged.

### 3. Recovery transition boundary

Path B will call a transport-aware helper rather than embedding DFU abort logic directly.

Classic DFU keeps the existing behavior. KIS DFU performs the explicit one-shot experimental reboot probe and then waits for Recovery.

Recovery acceptance requires an actual recovery mode from libirecovery (`0x1280..0x1283`), not merely the physical presence of `05ac:1881`.

### 4. Identity manipulation fix

`src/bypass/path_b_identity.c` will:

- remove the stale requirement for a live DFU `dev->usb` handle;
- use libirecovery exclusively once Recovery is reached;
- remove descriptor read-back verification after `setenv`;
- keep the existing ECID match and recovery-mode checks.

### 5. Wrapper behavior

`start-helpers.sh` will recognize `05ac:1881` as DFU-like mode and will not try to parse a classic DFU serial descriptor from it. It will rely on the updated binary for KIS identity.

The wrapper will also check whether the current user can open the Apple USB node and print the udev remediation if access is denied.

### 6. USB permissions

Add a repository-owned udev rule:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="05ac", ATTR{idProduct}=="1227", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="05ac", ATTR{idProduct}=="1281", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="05ac", ATTR{idProduct}=="1881", MODE="0660", GROUP="plugdev"
```

Applying it requires `sudo`, so the helper will provide copy-ready commands rather than silently modifying host policy.

## Testing strategy

The implementation follows test-first development.

### Unit and mock tests

1. PID classification:
   - `0x1227` -> classic DFU;
   - `0x1881` -> KIS DFU;
   - `0x1281` -> Recovery;
   - unrelated PID -> unsupported.
2. Path selection:
   - KIS A16 selects Path B;
   - KIS never selects Path A.
3. Transition state machine:
   - classic DFU uses the classic abort path;
   - KIS executes exactly one experimental reboot request;
   - unsupported KIS reboot falls back to guidance/waiting;
   - Recovery is accepted only for a recovery mode;
   - timeout returns a transport-specific error.
4. Identity manipulation:
   - Recovery with `dev->usb == NULL` is accepted;
   - `setenv` success is recorded;
   - `saveenv` failure is non-fatal but visible;
   - no descriptor read-back is required.
5. Existing suites remain green:
   - `make test`;
   - `make test-mocks`.

### Hardware verification

After applying the udev rule:

1. rebuild the binary;
2. run `tr4mpass --detect-only --verbose` and verify A16/ECID;
3. run `tr4mpass --dry-run --force-path-b` and verify no device write occurs;
4. run the experimental transition once;
5. record whether Recovery was reached and the exact failure/success point;
6. do not claim activation bypass success unless the device independently reports activated state.

## Safety and rollback

- Before Recovery is reached, the implementation performs no persistent device write.
- The experimental reboot request may reboot the phone; the selected approach explicitly accepts that risk.
- No erase/restore is initiated.
- Code rollback is a normal Git revert of the focused changes.
- Host-policy rollback is removal of the added udev rule.
- The existing device identity is retained in memory and used for ECID matching across mode transitions.

## Acceptance criteria

The work is complete when:

1. `05ac:1881` is detected as KIS DFU without treating it as classic DFU;
2. A16/CPID/ECID are reported correctly;
3. all automated tests and the build pass;
4. the experimental transition is attempted exactly once;
5. Recovery handling is correct if Recovery appears;
6. an unsupported transition produces a precise, non-destructive failure rather than a crash or false success;
7. the final report states the observed result and the remaining iOS 26.6.2 activation barrier.

### Verified hardware note (2026-11-26)

The single experimental `irecv_reboot` request returned success while the device was in KIS mode, but the physical USB PID remained `05ac:1881` and libirecovery never confirmed Recovery during the 120-second poll. Physical Recovery entry was therefore required. Once Recovery was active, libirecovery independently reported mode `0x1281` and the requested exact ECID, so the follow-up design added the explicit `--resume-recovery --ecid <ECID>` path rather than issuing another KIS reboot request.
