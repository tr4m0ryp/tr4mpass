# Stage 4 sequence and diagnostic pitfalls

```
prime          AbortPipeZero (+ control GETSTATE)
overwrite      SET_FEATURE(0x02,0x03,wIdx=0x80) 48 B  -> require STALL
halt           A SINGLE AbortPipeZero
recovery       GETSTATE polling (short then long) until a response
payload        DNLOAD 528 B (split into 64 B, or in one block)
suffix         16 bytes of zeros
manifest       DNLOAD 0 bytes -> MANIFEST-SYNC
walk           3× GETSTATUS (0x06 -> 0x07 -> 0x08): the UAF callback fires here
reset          ResetDevice + USBDeviceReEnumerate
```

Hard points:

- **Never** send `DFU_ABORT`/`CLR_STATUS` between stage 2 and stage 3:
  this corrupts the EP0 ring, the device re-enumerates, `kIOReturnNoDevice` in a loop.
  The `CLR_STATUS` (`skip_clrstatus = 0`) issued by the spray performs the transition.
- The STALL of the overwrite is **the proof** that the heap state survived: if it
  does not happen, the UAF is lost → fail fast rather than send a payload
  into a pristine DFU buffer.

---

---

## Diagnostic pitfalls

#### 6.1 The false success

If the payload chunks are all eaten (`-7`) but the suffix / zero-len / GETSTATUS
sequence is issued anyway, the BootROM walks through
`0x06 → 0x07 → 0x08` on an **empty buffer**. All stages "complete", no
PWND appears. Safeguard: counter of consecutive timeouts, explicit abort
instead of declaring "payload sent".

#### 6.2 `ret=-99` (`LIBUSB_ERROR_OTHER`) on the overwrite

This is `kIOUSBPipeStalled` (`0xe000404f`) reported by the backend **after**
a service re-acquisition, and therefore misclassified as a generic error. The log of the last
run shows it:

```
overwrite AArch64 (callback=0x180380000, 48 bytes)
iokit_usb: kIOReturnNoDevice -- stale IOService, re-activating handle and retrying
iokit_usb: DeviceRequestTO failed: IOReturn=0xe000404f wLenDone=0
[ERROR] overwrite did not STALL (ret=-99, expected -9)
```

→ fix the internal retry of `iokit_usb_ctrl_transfer()`: it must **re-map**
the IOReturn code of the second attempt (STALL, timeout, …) instead of falling into the
final `LIBUSB_ERROR_OTHER`. **[Done — see §8]**

#### 6.3 Device "detected but dead" after a bail-out

An abort in stage 4 leaves EP0 halted on the BootROM side. The device still enumerates
(so detection succeeds) but **every** GET_STATUS of the following stage 1 times
out. Two fixes in place:

- error path of `exploit_deliver()`: `iokit_usb_recover_bus()` (ResetDevice +
  ReEnumerate) instead of a simple `deactivate()`;
- pre-flight in `checkm8_exploit()`: a GET_STATUS; on failure, log
  `device unresponsive (stale EP0 from a previous run)` then bus recovery.
  Validated on device: the next run starts again in normal stage 1.

---

---

[← Previous](xhci-constraints.md) · [Index](README.md) · [Next →](open-leads.md)
