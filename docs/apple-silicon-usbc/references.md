# References and documentary sources

- gaster (palera1n): `checkm8_stage_patch()` (`gaster.c`), `usb_timeout` (5 ms),
  `send_usb_control_request_no_data()` (buffer of `w_len` zero bytes),
  `reset_usb_handle()` = `ResetDevice` + `USBDeviceReEnumerate`.
- palera1n README: USB-C / Apple Silicon warning (replug after exploit, hub).
- Repository history: `2242f19` (why the IOKit backend exists),
  `8cf45a9` (multi-packet DATA stages rejected), `4d08282`.

---

---

## Documentary research sources

#### 12d. "Low-level" options evaluated (user request, E16)

Question asked: "can we bypass at ultra low level — assembly
compiled to replace a lib, or use the palera1n code directly?"

| Lever | Verdict | Reason |
|---|---|---|
| **Write the transport in assembly** | **pointless** | The blocker is not computation but **kernel routing**: which user client (`IOUSBDeviceInterface` vs `IOUSBInterfaceInterface` vs `IOUSBHostDevice`) receives the transfer. No assembly instruction changes the kernel stack that decides the port reset. |
| **Use gaster (0x7ff) as-is** | **done** (E16) | Source identical to `gaster_ref.c` studied since the beginning, official A9 payloads. Binary compiled into `~/Documents/git/github/maxime1907/gaster/gaster`. Test pending the device. |
| **Use palera1n V3.X / libopenra1n** | **not viable** | (a) `checkm8.c` has a payload loop bug (`bool ret = false` never set to true) → never sends the payload; (b) transport identical to ours (`DeviceRequestTO`, shim.c); (c) A9 payload = yolo DFU (PID 0x4141), architecture different from gaster. |
| **IOUSBHost transport (modern framework)** | **real lever, untested** | `IOUSBHost.framework` (SDK 27.2 present) exposes `IOUSBHostDevice sendDeviceRequest:data:bytesTransferred:completionTimeout:error:` — a path **different** from the `IOUSBDeviceInterface320` of IOUSBLib that we have used since E5. It is candidate no. 1 if E16 shows that gaster also fails: same role, another kernel implementation. |
| **libciderra1n (checkra1n)** | **inaccessible** | Closed (binary), it is what palera1n calls on macOS for A9-A11. |

**Priority order**: E16 (gaster) first — a single execution
settles "our code vs the platform". Then IOUSBHost if needed.

#### 12c. Mechanics of the overwrite STALL (DSEC/Habr/ipwndfu agents, E11)

- **What the device does at STALL**: at SETUP, the DFU handler stores
  `ep0_data_phase_length` + `ep0_data_phase_buffer` and "blindly
  advances" the pointers — any incoming OUT packet is memcpy'd
  into the buffer, with no re-validation (littlelailo, Habr/DSEC "Data
  Stage"). After the UAF, the stale buffer = freed io_buffer: the
  overwrite's OUT packets are ACKed and DMA'd into it, then the
  request is rejected → the host observes STALL (littlelailo; King;
  gaster requires `USB_TRANSFER_STALL`).
- **The SecureROM STALL is deferred, not immediate** (Habr, a1exdandy
  comments): "the USB bus does not go into STALL (STALL handshake
  packets are not sent, only NACKs)" and the controller "still
  processes SETUP packets". The STALL observed by the host = logical
  rejection of the request, not a frozen endpoint.
- **0x02/0x03 SET_FEATURE(ENDPOINT_HALT) vs 0x00/0x00**: SET_FEATURE
  asks the device to halt its OWN EP0-IN (malformed with data
  phase); 0x00/0x00 = GET_STATUS in the wrong direction — invalid
  request rejected whose data phase nonetheless lands in the
  stale buffer, without setting the halt feature. ipwndfu upstream:
  `libusb1_no_error_ctrl_transfer(device, 0, 0, 0, 0, overwrite, 100)`
  (bmRequestType=0, bRequest=0, errors ignored). NB: ipwndfu
  upstream has NO A9/S8000 path (future); the A9 variant comes
  from the forks (King `checkm8_A8_A9`, overwrite {0,0,0,0,0x180380000,0}).
- **On the macOS host side**: libusb darwin does NO automatic
  CLEAR_FEATURE after a control transfer STALL (only the
  cancel path does AbortPipe; ClearPipeStallBothEnds only for
  IOUSBFamily ≤ 245). Neither ipwndfu nor gaster call clear_halt —
  they rely on the device re-arming EP0 at the next SETUP.
- **checkra1n M1 (official)**: the Apple Silicon USB-C stack does
  a DOUBLE USB reset on USBReEnumerate (not USB-A); with EP0_IN
  stalled and packets leaked in the queue, this triggers a NULL-deref →
  device crash/reboot on A7/A9X/A10/A10X (A9 not listed).
  checkra1n workaround: skip this step + manual replug. palera1n README:
  USB-C AS may require a replug, hub recommended.
- **Never published**: AS-XHCI vs Intel behavior for control transfer
  STALLs; `wLenDone` at STALL (no implementer reads it);
  our E10/E11 observations remain local hypotheses.

#### 12b. Analysis of the palera1n PR #765 clone (V3.X, sha 42d6506) — graphify + reading

Repo cloned into `~/Documents/git/github/maxime1907/palera1n-pr765`, graph
graphify in `palera1n-pr765/graphify-out/` (1,076 nodes, 2,151 edges,
62 communities; HTML `graph.html`).

**V3.X architecture**: `src/exploit.c` drives two handles (DFU 05ac:1227,
pongo 05ac:4141) in an infinite loop; RESET/SETUP/
SPRAY/PATCH/YOLODFU state machine; **`reset_usb_handle()` (ResetDevice+ReEnumerate)
between EACH step** — the handle is reopened at every stage, nothing
crosses the stages. The return from PATCH is **ignored** ("Checkmate?").

**libopenra1n `checkm8.c` patch stage (A9)** — major differences vs gaster:

1. **Different pre-overwrite**: `checkm8_usb_request_stall()` (sync
   SET_FEATURE) + `checkm8_usb_request_leak()` (async 1 ms), then **2×
   more SET_FEATURE(ENDPOINT_HALT)**, then overwrite.
2. **Overwrite shape**: `send_usb_control_request(handle, 0x00, 0,
   0, 0x00, overwrite, 48)` — bmRequestType=**0x00**, bRequest=**0**
   (like original ipwndfu; gaster uses 0x02/3 SET_FEATURE). STALL
   required.
3. **A9 overwrite: `callback.next = insecure_memory_base`** (null
   callback!) for ALL 64-bit — gaster A9 sets `callback = insecure_mem`.
   Different payload architecture: A9 stage1 = 0xc0 ROP chain
   (func_gadget 0x10000de0c, write_prim2, dc_civac, icache — NO
   ttbr0/tlbi) + embedded **yolo_s8000.bin** payload (YoloDFU: the device
   re-enumerates with PID 0x4141). "yolo DFU" approach ≠ gaster (PWND serial).
4. **BUG in the PR**: `bool ret = false` is never set to true →
   `for (i = 0; ret && i < data_sz; ...)` — **the payload loop is dead**;
   the patch stage NEVER sends the payload, returns TRANSFER_FAILED,
   which the driver ignores. The openra1n V3.X path cannot pwn in
   this state (WIP) — consistent with the fact that macOS/Linux use
   **libciderra1n** (closed, checkra1n 1337) and openra1n only iOS.
5. **User client interface**: `send_interface_control_request` /
   `WritePipe` are used ONLY in `pongo_helper.c` (pongo upload AFTER
   the exploit). The checkm8 flow NEVER uses the interface client →
   the E9 premise ("checkra1n transport = interface client") was
   false; checkra1n's checkm8 transport (ciderra1n) is closed.
6. USB_TIMEOUT = 5 everywhere, no override; `usb_handle_t` carries device
   AND interface clients open simultaneously (USBDeviceOpenSeize +
   USBInterfaceOpen) → E9's "exclusive conflict" hypothesis is false
   too; the 0xe0004051 of E9 remains unexplained (perhaps interface
   already claimed by the open device client, or pipe 0 of the interface
   not armed).

**THE cross-cutting clue (our own logs, stage 2)**: the padding
1280 B via **libusb** STALLs (`ret=-9`) then the next 64 B DNLOAD passes
(`ret=64`) on the SAME handle — **libusb/darwin survives a STALL with
data phase on M-series**; it is `DeviceRequestTO` (device user client)
that wedges. Yet stage 4 needs NO abort at all (ipwndfu/gaster/
openra1n: zero aborts post-overwrite) — the aborts were the reason for
the move to IOKit, not the STALLs.

---

[← Previous](commands.md) · [Index](README.md) · [Next →](journal/README.md)
