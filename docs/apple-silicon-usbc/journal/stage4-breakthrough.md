# Stage 4 breakthrough (E10–E19)

**Hypothesis**: after the IOKit spray (async+abort needed), reopen a
**libusb** handle and do ALL of stage 4 in libusb: overwrite SET_FEATURE
48 B (STALL `-9` expected, without consequence), payload 528 B, suffix 16 B,
zero-len, GETSTATUS ×3, then reset (IOKit ResetDevice). The STALL of
stage 2 (data phase + STALL then transfer OK) proves that libusb/darwin
does not wedge on this pattern.

**Test**: implemented — `exploit_deliver` (`checkm8.c`) disables IOKit
right after the spray (`iokit_usb_deactivate()`, closing the user client
WITHOUT bus reset → the sprayed heap survives) and reopens a libusb
handle (`usb_dfu_find`), then `checkm8_stage_patch` (`checkm8_patch.c`)
routes the whole of stage 4 to the common libusb path (helper
`stage4_libusb_path`: overwrite SET_FEATURE 48 B → STALL `-9` expected,
payload 528 B in chunks, suffix 16 B, zero-len, GETSTATUS ×3,
`libusb_reset_device`). The historic IOKit fallback remains available
if no libusb handle is available.

**Result** (`/tmp/tr4mpass-e10.log`, 3 identical attempts):
**falsified, but the run contains the missing deterministic discovery.**

Facts (identical across the 3 attempts):

1. **The overwrite via libusb STALLs and libusb reports it
   cleanly** (`ret=-9` PIPE, no host wedge) — the libusb transport
   survives the data-phase STALL as predicted.
2. **Everything after the overwrite times out** (`-7` ×6: payload 528,
   suffix, zero-len, 3× GETSTATUS) via libusb, on all three
   attempts — the payload never left.
3. The device survives: clean serial, DFU state 0x02, reusable.
4. **The device re-enumerates (addr 1→2) at the stage 3 CLR_STATUS**
   (wedge `0xe00002ed`), just before the patch.

**The natural experiment in the same log**: BOTH requests
STALL, but only one kills EP0:

| | stage 2 padding | stage 4 overwrite |
|---|---|---|
| Request | `0x00/0x00`, 1280 B | `0x02/0x03` SET_FEATURE(ENDPOINT_HALT), 48 B |
| STALL | `-9` | `-9` |
| Next | **`ret=64` OK** | **`-7` ×6, everything dies** |

Reading: **SET_FEATURE(ENDPOINT_HALT) sets the halt bit on EP0-IN
on the host side** (standard function: feature received by the device, but
the controller's endpoint context stays halted — no more TDs submitted
→ timeouts, whatever the user client or libusb). A simple rejection
(`0x00/0x00`) STALLs **without** setting the halt → EP0 stays alive.
This is exactly the overwrite shape of **ipwndfu** and **openra1n**
(`send_usb_control_request(handle, 0x00, 0, 0, 0x00, overwrite, 48)`)
; gaster (0x02/0x03) works on Intel Macs where the USB stack cleans up the
halt differently. The overwrite STALL was therefore also a false
positive: it also occurs on a fresh device.

**E11** = a single variable: the **overwrite shape**
`0x00/0x00` instead of `0x02/0x03` (openra1n/ipwndfu), everything else
gaster-exact (order, DeviceRequestTO, fire-and-forget, final reset).
Divergence accepted vs gaster, justified by the E10 log. Revert of the
libusb switchover (E10) in `exploit_deliver`; `stage4_libusb_path`
kept for Linux/Windows (`__attribute__((unused))` on macOS).

## E11 — Overwrite shape 0x00/0x00 (openra1n/ipwndfu), rest gaster-exact

**Hypothesis**: only the SET_FEATURE(ENDPOINT_HALT) shape wedges host
EP0 (halt feature set on the controller side). The `0x00/0x00` shape
rejects the request with STALL without halt → EP0 survives → payload/finalize
leave → MANIFEST → PWND.

**Test**: implemented — `checkm8_patch.c`: overwrite
`IOKIT_XFER(0x00, 0x00, 0, 0x80, &ow, 48, 5)`; TLBI prelude kept
(gaster 1215, skipped for A9); payload 528 B in ONE DNLOAD + suffix +
zero-len + 3× GETSTATUS via DeviceRequestTO, results logged and
ignored; final ResetDevice + ReEnumerate. Prime/GETSTATE/abort and
interface user client removed (everything that remained of E2..E9).
`exploit_deliver` no longer switches to libusb after the spray (E10 revert).

**Result** (`/tmp/tr4mpass-e11.log`): **the sequence goes through
entirely for the first time** (3 runs; 2/3 complete attempts,
1 aborted at the overwrite by `0xe00002d8` NotReady left over
from the CLR_STATUS wedge):

```
overwrite 0x00/0x00 : ret=-9 (STALL)      <- EP0 SURVIVES the STALL
payload  DNLOAD 528 : ret=528  (accepted!)
suffix   DNLOAD  16 : ret=16   (accepted)
zero-len DNLOAD   0 : ret=0    (accepted)
GETSTATUS[0..2]     : 0x06 -> 0x07 -> 0x08 (complete MANIFEST walk)
ResetDevice+ReEnum  : ok
```

But **no PWND** in the serial. The MANIFEST walk 0x06→0x07→0x08
happened, the device stayed in DFU with a clean serial: the
hijacked callback did NOT modify `gUSBSerialNumber`.

Two possible readings, discriminated by the `wLenDone` of the overwrite
STALL (logging added after the fact — E11b):

- **H1 (data lost)**: STALL with `wLenDone=0` → the DATA phase
  did not land (device rejected before mapping the buffer) →
  overwrite never written → payload allocated in a normal DFU buffer →
  MANIFEST walk of a clean download, without shellcode.
- **H2 (data placed, silent payload)**: STALL with `wLenDone=48` →
  overwrite landed but the gaster architecture (UAF io_request + TLBI
  ROP at MANIFEST) does not trigger on this device/host path.

Source verification (subagents, §12b/12c): gaster NEVER reads
`wLenDone` (only `.ret` counts) — no artifact from the
implementers on what `wLenDone` is at STALL. openra1n requires
`USB_TRANSFER_STALL` on 0x00/0x00 then continues without checking the
bytes. No CLEAR_FEATURE in gaster/openra1n/ipwndfu (the device
re-arms EP0 by itself at the next SETUP — Habr: the SecureROM
"always processes SETUP packets", STALL = logical rejection, not a
handshake that freezes the endpoint).

**E11b** (`/tmp/tr4mpass-e11b.log`): re-run of E11 with `wLenDone`
logged on the overwrite STALL (direct path AND post-reactivation).

**Result**: `wLenDone=0` **everywhere** — on the 48 B overwrite
STALL (both paths) AND on the spray's SET_FEATURE (expected:
wLength=0). **Ambiguous**: nothing proves that IOKit updates
`wLenDone` at STALL (gaster never reads this field; the overwrite
STALL has no readable equivalent on the libusb side). The
H1/H2 discriminant is therefore NOT settled — wLenDone=0 may be an
IOKit artifact as much as a device truth.

**New divergence found re-reading openra1n/ipwndfu** (sources
agent): their overwrite is `(0x00, 0x00, wValue=0, wIndex=0x00,
48)` — **wIndex=0x00**. Our E11 sent `wIndex=0x80` (inherited from
the gaster SET_FEATURE shape where wIndex=endpoint). For an invalid
0x00/0x00 request, wIndex has no semantic meaning, but the
SecureROM may reject differently depending on wValue/wIndex (early
validation at SETUP → STALL without data phase → wLenDone=0 would then be
the device truth = H1).

**E12** = overwrite **exactly openra1n/ipwndfu**:
`(0x00, 0x00, 0, 0x00, 48)` — a single variable (wIndex 0x80→0x00).

**Result** (`/tmp/tr4mpass-e12.log`): **falsified** — signature
identical to E11 (overwrite `-9` wLenDone=0, payload 528 accepted,
complete MANIFEST, no PWND; 2 complete attempts, 1 aborted
`0xe00002d8` — the NotReady retry was not yet compiled into that
binary). wIndex changes nothing.

Incidentally: `SetConfiguration(1)` at IOKit activation is indeed
present in gaster (L335) AND palera1n (L143) — not a divergence.
The entry AbortPipeZero of stage 3 remains a minor divergence
(references: only in the async+abort path).

**E13** = skip the **stage 3 CLR_STATUS** (env
`TR4MPASS_SKIP_CLRSTATUS=1`). Reason: the `0xe00002ed` wedge +
re-enumeration it causes on M-series is the last device-visible event
before the overwrite; ipwndfu NEVER sends a
CLR_STATUS in the A9 path (stall→leak→no_leak→usb_reset→
overwrite→payload→usb_reset).

**Result** (`/tmp/tr4mpass-e13.log`): **decisive.** Without the
CLR wedge, the overwrite STALLs (`-9`, wLenDone=0) on the original
handle — and **the payload DNLOAD dies** (`-7` ×6: payload,
suffix, zero-len, 3× GETSTATUS), on all 3 attempts (including 2 with
a real STALL at stage 2, not the timeout-as-stall).

**Revised synthesis (important)**: I re-read the past logs —
E10's "natural experiment" was **wrong**: stage 2 attempts 1-2
(dnload `sent=2048` then `ret=64`) NEVER STALLed
(normal completion before abort); the padding STALL only happens at
attempt 3, followed by a handle close. So:

- **EVERY EP0 STALL wedges the current handle** (libusb AND
  DeviceRequestTO), whatever the shape (0x00/0x00 as well as
  0x02/0x03) — E10's "libusb survives the STALL" was a misreading.
- E11/E12: the payload was accepted (528) **only on a freshly
  re-activated handle** (the NoDevice had already forced a
  reactivation BEFORE the overwrite STALL) — that is, on a
  freshly enumerated device → consistent with H1 (normal DFU device,
  overwrite never landed, MANIFEST walk of a clean download).
- E13 proves that on the NON-re-enumerated handle, after the overwrite
  STALL, nothing more passes.

**E14** = E13 + **a single variable**: rebuild the IOKit handle
between the overwrite and the payload (deactivate + activate — SetConfig
as gaster L335). The device-side state survives the host re-enumeration
(the overwrite still STALLs after the CLR re-enum in E11/E12).
If payload accepted + PWND → win. If accepted without PWND →
overwrite does not land → E15 = the same with the gaster 0x02/0x03 shape
(littlelailo: "set configuration handler ignores wLength" — the
data phase lands via the hacked handler, which 0x00/0x00 does not
guarantee).

**Result** (`/tmp/tr4mpass-e14.log`): **falsified** — payload
`-7` ×6 after handle rebuild, 3/3 attempts. BUT the log
contains the decisive fact:

```
overwrite → STALL (-9)
iokit_usb: deactivated
iokit_usb: DFU service re-acquired after 2 polls (250 ms)
           -- device re-enumerated      <-- we only closed OUR client
```

**The DEVICE re-enumerates by itself after the overwrite STALL.** So it
is not the host pipe that is wedged: the device resets its USB
stack. The whole following sequence (payload accepted in E11/E12, or
timeout in E13/E14) plays out on a **fresh** device → consistent with
the absence of PWND in ALL runs since E5b.

Still to explain why the device re-enumerates. Two candidates:
(a) the EP0 STALL triggers a port reset on the host side (IOKit) — a
transport that does not reset the port is then needed; (b) the SecureROM
crashes on the request (Apple Silicon USB-C double-reset, checkra1n M1
note) — but A9 is not in the list of affected chips.

**E15 (lead)**: re-read libopenra1n's `shim.c` — agent no. 3 had
truncated the answer exactly at `send_usb_control_request_no_data`,
and libopenra1n opens the **interface** user client IN ADDITION to the device
client. If their checkm8 flow routes transfers through
`IOUSBInterfaceInterface::ControlRequest` (interface pipe 0), the
STALL may not involve the device client's port reset.

**shim.c lead result**: **eliminated.** `shim.c` (62 lines, read in
full): `send_usb_control_request_no_data` simply calls
`send_usb_control_request`, which is `DeviceRequestTO` on the device
user client. libopenra1n therefore uses exactly the same transport
as we do; the interface user client is used ONLY for the post-exploit
pongo upload. What remains, then, as the only difference, is the shape of
the overwrite (0x00/0x00) — already tested in E11/E12.

## E16 — Running the REFERENCE implementation (gaster from 0x7ff)

**Question**: is it OUR port that is at fault, or can this Mac +
this cable + A9 simply not be pwned by checkm8 at all?

**Test**: `git clone https://github.com/0x7ff/gaster` (author of
palera1n, source of the `gaster_ref.c` studied since the beginning) into
`~/Documents/git/github/maxime1907/gaster`, compiled in place
(`make` → 71,800-byte binary, 14 benign warnings), then
`./gaster pwn` in a pseudo-terminal (its `while(stage !=
STAGE_PWNED)` loop never exits on its own; output buffered otherwise).

**Result**: **SUCCESS — `PWND:[checkm8]` obtained** on this M-series Mac
+ USB-C→Lightning cable **direct**, without hub, without replug.

```
Stage: RESET  ret: true
Stage: SETUP  ret: true
Stage: SPRAY  ret: true
Stage: PATCH  ret: true
Now you can boot untrusted images.
USB Serial Number = "CPID:8000 ... SRTG:[iBoot-2234.0.0.3.3] PWND:[checkm8]"
```

**Verdict**: checkm8 IS possible on this platform → our port has
a precise defect. The journal switches to "diff against the winning
binary" mode.

## E16bis — Structural diff of our stage 3/4 ↔ winning gaster

Full read of `~/Documents/git/github/maxime1907/gaster/gaster.c`
(today's 0x7ff clone, the version that just succeeded) + dump of its
A9 config via instrumentation (`GASTER_DUMP=/tmp/gaster-e16.txt`):

**Identical to ours (verified):**
- table 0x8000: all addresses identical to our `chip_db`
  (patch_addr 0x10000812C, memcpy 0x100011030, gUSBSerialNumber
  0x180087958, dfu_handle_request 0x1800878F8, usb_core_do_transfer
  0x10000EE78, dfu_handle_bus_reset 0x180087928,
  handle_interface_request 0x10000F1B0, usb_create_string_descriptor
  0x10000E354, usb_serial_number_string_descriptor 0x1800807DA,
  insecure_memory_base 0x180380000, boot_tramp_end 0x1800E1000,
  config_overwrite_pad 0x500) ;
- payload: `data_sz = (280−104) + 104 + (248−56) + 56 = 528` — layout
  identical to ours (main 176 + tail 104 + handler 192 + handler_tail
  56), **no ROP for A9** (tlbi=0, ttbr0_sram_off=0), fields
  filled in the same order, same tag ` PWND:[checkm8]`. Our
  `assemble_payload` is logically identical.
- A9 spray: `stall (0x02,0x03,0,0x80,0)` + `leak (GET_DESCRIPTOR 64)`
  + `no_leak (193)` + `CLR_STATUS (193 zeros, sync, result ignored)`.

**Structural differences (candidates for the defect):**

1. **Strict separation of stages**: after EACH stage, gaster does
   `ResetDevice(handle->device)` + `USBDeviceReEnumerate(handle->device, 0)`
   + handle close, then reopens a **fresh** handle for the next
   stage (`wait_usb_handle` loop). Line 1268/1273. Ours: stages
   3 and 4 on the same handle (re-activated only after wedge).
2. **Overwrite shape: `(0x02, 0x03, 0, 0x80, 48)`** — the
   SET_FEATURE(ENDPOINT_HALT) shape WITH 48 bytes of data, and it REQUIRES the
   STALL (`transfer_ret.ret == USB_TRANSFER_STALL`), then continues the
   payload **on the same handle, fire-and-forget** (the result of the
   DNLOAD is not checked), then suffix 16, zero-len 0, GETSTATUS ×3
   (results ignored), then ResetDevice+ReEnumerate.
   ⇒ Our "M-series rule no. 3" ("SET_FEATURE sets the host halt →
   use 0x00/0x00") is **FALSIFIED by E16**: gaster uses
   0x02/0x03 successfully on this machine.
3. **The ResetDevice+ReEnumerate AFTER the GETSTATUS walk** triggers
   `dfu_handle_bus_reset` → payload execution → PWND in the serial.
   Our E11/E12 also did a final ResetDevice+ReEnumerate, but with
   a 0x00/0x00 overwrite that probably never landed (H1:
   the normal DFU device accepts the payload into its download buffer
   → clean MANIFEST walk = false success).

**Unified hypothesis post-E16**: the overwrite must be 0x02/0x03 (the
request reaches the ROM's EP0 handler and its 48-byte data phase
lands in the freed io_buffer; 0x00/0x00 is rejected at SETUP
**without data phase** → nothing lands), and the device must be reset
to zero (ResetDevice+ReEnumerate) between spray and patch — the STALL of
the overwrite and the payload only go through on this freshly
reset device.

**E17 (protocol)**: instrument gaster to trace EVERY transfer
(bmRequestType, bRequest, wValue, wIndex, wLength, IOReturn, wLenDone,
stage boundaries), return the device to normal DFU, re-run `pwn`,
capture the exact winning sequence, then port it verbatim into
`iokit_usb.c`/`checkm8_*` (E18).

## E18 — Porting the winning sequence (E16bis diff applied)

**Changes in our code** (a single validation run afterwards):

1. `checkm8_patch.c`: overwrite `(0x00,0x00,0,0x00,48)` →
   **`(0x02,0x03,0,0x80,48)`** (gaster exact, STALL required) — ARMv7 and
   AArch64;
2. `checkm8_patch.c`: **removal of the E14 rebuild** between overwrite
   and payload (gaster continues on the SAME handle, gaster.c L1211-1216);
3. `checkm8.c`: **inter-stage reset** spray→patch
   (ResetDevice + ReEnumerate + deactivate + 250 ms + activate —
   gaster's strict stage separation, L1268/1273);
4. `checkm8_patch.c`: E15 diagnostic (pre-overwrite GETSTATUS)
   **disabled by default**, re-enabled by `TR4MPASS_PRE_STATUS=1` —
   the winning run sends NO request between the end of the spray and
   the overwrite, and one extra request is a divergence.

**Minor divergence accepted**: `iokit_usb_activate()` sends an
`AbortPipeZero` when opening the handle; gaster does not
(the abort only exists in its async path). On a fresh handle,
nothing is in flight → no-op. Kept.

**E18 timeouts**: all of stage 4 sync goes to **5 ms** (gaster
`usb_timeout` — the winning config), override possible via
`TR4MPASS_STAGE4_TIMEOUT_MS=5000` to reproduce the E11/E12
behavior. Before this change, our payload/suffix/GETSTATUS
used `USB_TIMEOUT_MS=5000` — 4 more divergences vs gaster.

The stage 3 CLR_STATUS stays ENABLED (gaster sends it, 193 zeros, sync,
result ignored — cf. E13 for the wedge behavior, which is also
gaster's on this machine since its run wins).

Clean build, 62/62. Validation run when the device is in normal
DFU.

**E18 result** (`/tmp/tr4mpass-e18.log`): **MAJOR PROGRESS —
the overwrite really landed, proven by crash.**

- overwrite `0x02/0x03` clean STALL (wLenDone=0);
- payload/suffix/zero-len/GETSTATUS: `-7` ×6, `wLenDone=0` everywhere
  → **the payload data phase NEVER left** (not a STALL:
  a timeout);
- **but the device LEFT DFU and rebooted under iOS** (UDID serial,
  never seen since E5b where it stayed cleanly in DFU). Interpretation:
  the hijacked callback (0x180380000) **executed** on bus reset — but
  the buffer at that address did not contain our payload (data
  phase never transferred) → spray garbage → exception → reboot.
  This is the first time the callback actually EXECUTES:
  the weapon is armed, only the landing of the 528 bytes is missing.

Verification done at the same time: our embedded blobs
(`payload_A9_bin` 280, `payload_handle_checkm8_request_bin` 248) are
**byte-exact identical** to the upstream 2026 bins that won → the
payload is not at fault. What remains, then: the mechanics that get
the 528 DNLOAD through after the overwrite STALL in gaster.

**E17 = THE decisive measurement**: request-by-request trace of the winning
gaster (`GASTER_TRACE`, instrumentation compiled into the binary —
SYNC/ASYNC_SUBMIT/ASYNC_DONE + stage boundaries). Line-by-line
comparison against our E18 log: every divergence (stage 1
reset: gaster does a full DNLOAD16+MANIFEST+DNLOAD64 cycle,
we just ABORT/CLRSTATUS; spray; CLR; overwrite→payload) will be
visible in black and white.

## E17 — RESULT: full trace of the winning run (device in normal DFU)

Run executed by the user, device in normal DFU, instrumented
gaster. **PWND obtained** ("Now you can boot untrusted images.").
Raw trace verbatim (`/tmp/gaster-trace.log`, format:
`SYNC|ASYNC_SUBMIT|ASYNC_DONE bmReq bReq wValue wIndex len= ret= sz=`,
IOReturn in hex, stage boundaries):

```
SYNC 80 06 0100 0000 len=18 ret=0x0 sz=18
SYNC 80 06 0304 0409 len=255 ret=0x0 sz=196
=== STAGE RESET ===
SYNC 21 01 0000 0000 len=16 ret=0x0 sz=16
SYNC 21 01 0000 0000 len=0 ret=0x0 sz=0
SYNC a1 03 0000 0000 len=6 ret=0x0 sz=6
SYNC a1 03 0000 0000 len=6 ret=0x0 sz=6
SYNC a1 03 0000 0000 len=6 ret=0x0 sz=6
SYNC 21 01 0000 0000 len=64 ret=0x0 sz=64
SYNC 80 06 0100 0000 len=18 ret=0x0 sz=18
SYNC 80 06 0304 0409 len=255 ret=0x0 sz=196
=== STAGE SETUP ===
ASYNC_SUBMIT 21 01 0000 0000 len=2048 abort=4
ASYNC_DONE 21 01 0000 0000 len=2048 sz=2048 status=0
SYNC 21 01 0000 0000 len=64 ret=0x0 sz=64
ASYNC_SUBMIT 21 01 0000 0000 len=2048 abort=5
ASYNC_DONE 21 01 0000 0000 len=2048 sz=2048 status=0
SYNC 21 01 0000 0000 len=64 ret=0x0 sz=64
ASYNC_SUBMIT 21 01 0000 0000 len=2048 abort=0
ASYNC_DONE 21 01 0000 0000 len=2048 sz=64 status=1
SYNC 00 00 0000 0000 len=1216 ret=0xe000404f sz=0
SYNC 80 06 0100 0000 len=18 ret=0x0 sz=18
SYNC 80 06 0304 0409 len=255 ret=0x0 sz=196
=== STAGE SPRAY ===
SYNC 02 03 0000 0080 len=0 ret=0xe000404f sz=0
ASYNC_SUBMIT 80 06 0304 000a len=64 abort=1
ASYNC_DONE 80 06 0304 000a len=64 sz=0 status=1
ASYNC_SUBMIT 80 06 0304 000a len=193 abort=1
ASYNC_DONE 80 06 0304 000a len=193 sz=0 status=1
SYNC 21 04 0000 0000 len=193 ret=0xe00002ed sz=0
SYNC 80 06 0100 0000 len=18 ret=0x0 sz=18
SYNC 80 06 0304 0409 len=255 ret=0x0 sz=196
=== STAGE PATCH ===
SYNC 02 03 0000 0080 len=48 ret=0xe000404f sz=0
SYNC 21 01 0000 0000 len=528 ret=0xe0004051 sz=0
SYNC 21 01 0000 0000 len=16 ret=0xe0004051 sz=0
SYNC 21 01 0000 0000 len=0 ret=0xe0004051 sz=0
SYNC a1 03 0000 0000 len=6 ret=0xe0004051 sz=0
SYNC a1 03 0000 0000 len=6 ret=0xe0004051 sz=0
SYNC a1 03 0000 0000 len=6 ret=0xe0004051 sz=0
SYNC 80 06 0100 0000 len=18 ret=0x0 sz=18
SYNC 80 06 0306 0409 len=255 ret=0x0 sz=226
```

(Without stage boundaries: reading the serial at each `wait_usb_handle` —
18 B descriptor + 196 B normal serial between each stage; the last
`sz=226` = **196+30 = PWND:[checkm8] added**.)

---

[← Previous](early-runs.md) · [Index](README.md) · [Next →](stage4-winning.md)
