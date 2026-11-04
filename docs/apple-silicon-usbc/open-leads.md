# Open leads and repository state

1. **Why the PWND does not appear although the MANIFEST walk passes.**
   ~ Elucidated (§11, Run A decoded): the payload DNLOAD carried a 5 s
   budget while the ring re-arms at ~10 s — its bytes never reached
   the DFU buffer; MANIFEST was working on an empty buffer.
2. **EP0 recovery window after the STALL.** Measured: ~10 s (Run A:
   transfer submitted at t+5 s completed at t+10 s). A transfer whose budget
   straddles the recovery moment completes normally.
3. **Is an `AbortPipeZero` needed after the overwrite STALL?** Settled:
   **NO.** A/B evidence accumulated over 6 runs (§11) — it is the poison.
4. ~~Payload timeout budget.~~ Fixed: `USB_EP0_RECOVERY_TIMEOUT_MS`
   (30 s) on the payload DNLOAD (E6, device validation in progress).
5. **The `0xe00002ed` at the end of the spray.** Resolved on the loop side: the
   async+abort transport (abort **while** the request is pending) makes the
   stall/leak/no_leak loop perfectly clean (E5: 100 iterations without a single
   wedge). The final CLR_STATUS still wedges the device once (0xe00002ed,
   CLR `-99`) but is **without consequence**: the service comes back in 250 ms,
   the heap state survives, and stage 4 continues normally.
6. **Alternative transport (USB-A hub).** Still relevant as a "known-good"
   reference for isolating what belongs to the controller vs the device.

---

---

## Repository state

Files involved:

| File | Role |
|---|---|
| `src/exploit/iokit_usb.c` | IOKit backend: activate (IORegistry polling), transfers, abort, serial, reset, `recover_bus` |
| `src/exploit/checkm8.c` | orchestration, libusb→IOKit switch, pre-flight, `checkm8_verify_pwned`, retry |
| `src/exploit/checkm8_stages.c` | transfer helpers, stage 1, stage 2 |
| `src/exploit/checkm8_spray.c` | stage 3 (spray) + `CLR_STATUS` |
| `src/exploit/checkm8_patch.c` | stage 4 |
| `include/exploit/checkm8_internal.h` | constants (including `GASTER_USB_TIMEOUT_MS`) |
| `src/device/usb_dfu.c` | serial reading (UTF-16LE, no LANGID probe) |

Status at the time of writing:

- **validated on device**: stage 1, stage 2 (macOS timeout-as-STALL), stage 3
  (spray `stall+leak+no_leak` via gaster async+abort transport — succeeded
  first try once the transport was fixed, see §11/E5), pre-flight/
  bus recovery, service re-acquisition, serial reading.
- **decision**: the faithful copy of the gaster flow (payload in a single DNLOAD,
  result ignored, 5 ms timeouts, no abort after STALL) was first
  **reverted** (option A) then partially re-adopted: the async+abort transport
  of the spray and the ignored 193-zeros CLR are gaster, but stage 4 keeps the
  M-series adaptations (rationale: §11, the post-STALL abort is a poison and the
  payload must carry a long timeout).
- **fixes applied**: IOReturn re-mapping of the second attempt (§6.2);
  `iokit_usb_ctrl_transfer_async_abort()` (gaster async transport); gaster
  semantics on the async result (`sz == 0` is enough, not the status).
- **not validated**: stage 4 in the E6 version (payload DNLOAD with a 30 s budget
  straddling the ~10 s recovery window, without abort post-STALL).
  Full history of the trials: §11.

Reference commits for the history of decisions:
`2242f19`, `8cf45a9`, `4d08282`.

---

---

[← Previous](stage4-sequence.md) · [Index](README.md) · [Next →](commands.md)
