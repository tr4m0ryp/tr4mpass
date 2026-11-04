# XHCI constraints on Apple Silicon

## 3.1 A STALL does not self-clear the EP0 halt

On Intel/libusb, after a `SET_FEATURE(ENDPOINT_HALT)` that STALLs, the
next request resumes normally. On Apple Silicon XHCI, it does not: EP0 remains halted
on the controller side and any following request is rejected until a
`USBDeviceAbortPipeZero` has been issued.

## 3.2 Multi-packet DATA stages do not "complete"

A 528-byte `DFU_DNLOAD` = 9 packets of 64. Via `DeviceRequestTO`, the
request returns `kIOReturnTimeout` (`ret=-7`) even though the DATA phase was indeed
DMAed. This is exactly why gaster **ignores** the result of its
payload DNLOAD: it never needed to check it, and on M-series this
return value is systematically misleading.

Splitting into 64-byte packets was tried: it works (each DNLOAD fits
in a single packet), but it is **not enough** to make the PWND appear (see
§7). Splitting is therefore an option, not a necessary condition.

## 3.3 A single `AbortPipeZero` after the STALL — never two

A/B evidence on device:

| | Run A (1 `AbortPipeZero`) | Run B (2 `AbortPipeZero`) |
|---|---|---|
| After STALL + abort | suffix `16` OK, zero-len `0` OK, GETSTATUS `0x06→0x07→0x08` | 15 consecutive transfers `-7`, permanent |
| Conclusion | the controller "swallows" 1–2 transfers then re-arms | the 2nd abort re-poisons the ring |

Corollary: the ring re-arms **after ~10–11 s**, not after N
transfers. In Run A, two transfers with a 5 s timeout were "eaten"
before the third one (the suffix) went through. A repeated short poll (500 ms) does
not measure the same thing: each transfer cancellation is a mini-abort, and
that is precisely what reproduced the pathology of Run B.

## 3.4 Long timeouts turn a recoverable failure into loss of service

Observed chain:

```
0xe00002ed  kIOReturnNotResponding   (end of spray, or ambiguous request left pending)
   └─> the IOService service is torn down
       └─> next transfer: 0xe00002c0 kIOReturnNoDevice
           └─> re-open: "DFU device not found" if one does not wait
```

An ambiguous request (leaked `GET_DESCRIPTOR`, `CLR_STATUS` with a DATA phase not
acknowledged) left pending **for several seconds** escalates to
`NotResponding`. With a budget of a few milliseconds, the controller cancels
cleanly and the handle survives. This is the reason for the "gaster" timeout
`GASTER_USB_TIMEOUT_MS = 5` introduced in `include/exploit/checkm8_internal.h`.

## 3.5 The service can disappear and then come back — this is normal and recoverable

Re-enumeration of the device typically takes **250 ms to a few seconds**.
An `IOServiceGetMatchingService` in a single attempt within that window fails
outright (`DFU device not found`) even though the device comes back just after.

Fix applied: `iokit_usb_activate()` **polls the IORegistry**
(24 × 250 ms ≈ 6 s) instead of a single match, plus self-healing in
`iokit_usb_ctrl_transfer()` / `iokit_usb_abort_pipe_zero()`: if the handle is
inactive, an attempt is made to re-open it before returning `-4`.

Important: **the heap state of stage 2/3 survives the re-enumeration** on the device side.
Evidence: after `NoDevice` + re-acquisition, the `overwrite` still STALLs
(`STALL confirmed`) — the freed `io_buffer` is still there.

## 3.6 The serial is not read through the LANGID path

`libusb_get_string_descriptor_ascii()` probes the LANGIDs first; on Apple
Silicon this probe times out and **poisons the pipe** (the GET_STATUS calls of
stage 1 then fail). The code therefore reads the descriptor in UTF-16LE
directly (`read_string_descriptor_utf16_ascii`, `src/device/usb_dfu.c`).

---

---

## Useful IOReturn codes

| Code | Name | Practical interpretation |
|---|---|---|
| `0xe00002c0` | `kIOReturnNoDevice` | service dead → re-acquire (often a re-enumeration in progress) |
| `0xe00002d8` | `kIOReturnNotReady` | service not ready → wait / re-activate |
| `0xe00002ed` | `kIOReturnNotResponding` | request pending too long → service torn down |
| `0xe00002c5` | `kIOReturnExclusiveAccess` | libusb still holds the device |
| `0xe000404f` | `kIOUSBPipeStalled` | **this is the expected STALL** of the overwrite |
| `0xe000400e` | `kIOUSBNotSent1Err` | DATA stage not sent (multi-packet) |

DFU states: `0x02` dfuIDLE, `0x05` dfuDNLOAD-IDLE, `0x06` dfuMANIFEST-SYNC,
`0x07` dfuMANIFEST, `0x08` dfuMANIFEST-WAIT-RESET.

---

---

[← Previous](why-gaster-fails.md) · [Index](README.md) · [Next →](stage4-sequence.md)
