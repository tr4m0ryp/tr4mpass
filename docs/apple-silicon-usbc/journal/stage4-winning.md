# Winning sequence (E17-first-thoughts–E19)

**P1 — THE winning DNLOAD payload ALSO FAILS on gaster**:
`SYNC 21 01 len=528 ret=0xe0004051 sz=0` — IOKit timeout, 0 bytes
reported, **exactly our E18** (`-7` = 0xe0004051 mapped). suffix
16, zero-len, GETSTATUS ×3: `0xe0004051` on his side as on ours.
⇒ the USB result of the POST-overwrite has **no importance**: on his
side everything "fails" at the IOKit level and the PWND appears anyway.
Direct consequence: the data phase of the 528 B is **DMA'd device-side
BEFORE the STATUS timeout** (or is not at all — the PWND comes from
elsewhere); `wLenDone/sz=0` at timeout is an **unreliable host
artifact** (E11b). Our reading "payload never left" (E18) is
probably **wrong**.

**P2 — Since the post-overwrite is identical, the divergence is BEFORE the PATCH.** Wire-diff E17↔E18, stage by stage:

| Point | winning gaster | our E18 |
|---|---|---|
| STAGE RESET | full cycle: DNLOAD 16 OK, zero-len OK, **3× GETSTATUS OK** (MANIFEST walk), DNLOAD 64 OK | GETSTATUS alone (state 0x02) + dfu_reset_to_idle no-op — **NEVER a cycle** |
| SETUP attempt 1-2 | async 2048 **complete** (sz=2048) + DNLOAD 64 recovery | same (sz=2048, recovery 64) — identical |
| SETUP attempt 3 (abort=0) | async 2048: **device STALL after 64 B accepted** (`sz=64 status=1`) — 64 B IN the buffer | libusb cancel at 0 ms: **sz=0, NOTHING left** (status=3 cancelled) |
| padding | `(0,0,0,0,1216)` **STALL** (`0xe000404f`) | `(0,0,0,0,1280)` timeout-as-stall (`-7`) **different size** (1216 vs 1280!) |
| SPRAY | identical (stall/leak/no_leak/CLR wedge 0xe00002ed — **same IOReturn as ours**) | identical |
| PATCH | overwrite 48 STALL then everything `0xe0004051` | identical |
| after final reset | device COMES BACK in DFU, serial **sz=226 = PWND** | device **gone under iOS** (reboot) |

**P3 — Two real wire differences before the PATCH**:
(a) stage 1: gaster forces the DNLOAD16+walk+DNLOAD64 cycle (deterministic DFU state: the device goes through MANIFEST and comes back clean);
(b) SETUP attempt 3: on gaster's side, 64 B land device-side in the buffer BEFORE the STALL (`sz=64 status=1`); on ours the libusb cancel leaves before any byte (`sz=0`) ⇒ **our freed buffer is empty, his contains 64 B**. The padding differs accordingly (1216 = 0x500−64 on his side; 1280 on ours).

**P4 — The reboot-under-iOS of E18 re-explains itself**: if the data phase of the DNLOAD 528 lands device-side anyway (P1), then on OUR side it may have arrived at a **wrong place** (empty buffer at the wrong offset, different io_request chain because of (b)) → payload executed at a shifted address → crash → iOS reboot. On gaster, everything lands exactly (thanks to (a)+(b)) → PWND.

**P5 — Open question for the subagents**: does the DNLOAD 528 post-STALL (host-side timeout) really transfer its data phase device-side? If YES, P4 explains everything and the fix = replicate (a)+(b) exactly. If NO, the PWND comes from another path (to be identified). Discriminating test possible without a device: log `wLenDone` via a control OUT transfer deliberately timed out toward some hub and observe whether IOKit reports bytes (reliability of the field at timeout).

**P6 — Candidate E19 corrections (strict gaster order)**:
1. stage 1 = full gaster cycle (DNLOAD16 → zero-len → 3×GETSTATUS
   MANIFEST walk → DNLOAD64) instead of GETSTATUS+reset_to_idle;
2. stage 2 attempt 3: the libusb cancel at abort=0 must let the first 64 B packet through as on gaster (try abort=1 ms? or submit then AbortPipeZero immediately on the IOKit side — gaster uses DeviceRequestAsyncTO+sleep(0)+USBDeviceAbortPipeZero, WE use libusb cancel);
3. padding = `0x500 − sz` where sz = bytes actually left (64 on gaster, 0 on ours) — follows from (2);
4. check that our inter-stage reset between spray and patch does not differ from his (ResetDevice+ReEnumerate, close, reopen).

## E19 — Full port of the verdicts (P1–P6 applied)

**Subagent verdicts** (2/3 delivered; the 3rd, a read-only audit, refused — redone by hand, findings below):

- **P5 = H1, ~95% confidence** (`/tmp/sb-p5-verdict.md`, 17 sources):
  on IOKit `DeviceRequestTO`, a host timeout `wLenDone=0` **says nothing
  about the bytes already ACKed device-side** — the data phase of a
  timed-out DNLOAD can be fully delivered. Internal empirical proof:
  the winning run reports `ret=0xe0004051 sz=0` on the 528 payload
  **and the PWND appears** — the 528 B exist only in that data
  phase. Same for the overwrite STALL (48 B): sz=0 AND hijack
  confirmed. ⇒ **our E18 conclusion "payload never left" was
  wrong**: the payload probably arrived, but in a bad buffer
  state (P4 confirmed).
- **P3 port** (`checkm8_stages.c`, 177+/54−): (1) stage 1 =
  exact gaster cycle DNLOAD16 → zero-len → 3×GETSTATUS (MANIFEST
  walk verified by (status,state)) → DNLOAD64, all in
  `DeviceRequestTO` 5 ms, failure = CLRSTATUS + fail-fast; (2) stage 2
  attempt 3 = `DeviceRequestAsyncTO` + immediate abort via
  `iokit_usb_ctrl_transfer_async_abort` (same infra as
  request_stall/leak — direct port of gaster's IOKit
  `send_usb_control_request_async`) → target `sent=64` + STALL; (3) padding =
  `0x500 − real_sz` (1216 if 64). Build 0 warnings, 62/62.

**Orchestration audit (redone by hand) — 4 divergences found, all fixed** (`checkm8.c`):

1. **The winning run is IOKit end to end**: gaster's `make macos`
   compiles WITHOUT `-DHAVE_LIBUSB` → the 4 stages run on
   `DeviceRequestTO`/`AsyncTO` (the raw IOReturns `0xe000404f`/
   `0xe0004051` of the trace prove it). Ours: stages 1-2 in
   libusb → attempt 3 took the libusb cancel (`sent=0`, the
   full P3b divergence). **Fixed: IOKit enabled BEFORE stage 1**
   (`usb_ctrl_transfer_raw` already routes to IOKit when active);
   `checkm8_stage_reset` guard relaxed (NULL libusb handle allowed
   if IOKit active).
2. **Inter-stage reset missing reset→setup**: gaster resets after
   EVERY stage (L1347 in the loop). Ours: only spray→patch.
   The new stage 1 cycle ends with a DNLOAD 64 loaded → stage 2
   had to start on a fresh handle. **Fixed: reset after every
   stage** (helper `dfu_iokit_interstage_reset`: ResetDevice +
   ReEnumerate + deactivate + 250 ms + activate), at the 3 boundaries.
3. **Missing FINAL reset = the execution trigger**: after the
   last stage, gaster does reset_usb_handle **unconditionally**
   before close (L1347→1352) — it is THIS ResetDevice+ReEnumerate that
   triggers the hijacked `dfu_handle_bus_reset` callback, i.e. **that
   executes the payload**. Ours: plain `deactivate()` without reset.
   **Fixed: final reset before deactivate**, then `checkm8_verify_pwned`
   (which already handles the IOKit path) re-reads the re-enumerated serial → PWND.
4. **Verified with no divergence**: A9 spray (stall/leak 64/leak 193/
   no_leak 193/CLRSTATUS len=193, IOReturns identical to the trace),
   overwrite 0x02/0x03+48 B STALL, payload 528+suffix 16+zero-len+
   GETSTATUS×3 in 5 ms results-ignored, E15 behind
   `TR4MPASS_PRE_STATUS` (default OFF). gaster details noted: its
   spray loop relooped stall/leak/no_leak until success and it
   requires the overwrite STALL (otherwise failure → full retry) — our
   paths log and continue, acceptable for now.

**Build/tests**: 0 warnings, 62/62.

**Validation run (to be done by the user)** — device in normal DFU (it rebooted under iOS in E18):

```sh
cd /Users/mleroy/Documents/git/github/maxime1907/tr4mpass && ./tr4mpass -v 2>&1 | tee /tmp/tr4mpass-e19.log
ioreg -p IOUSB -l -w0 | grep -i "USB Serial Number"
```

Expected gaster-exact: stage 1 = 6 SYNC requests all `ret=0`
(16/0/6/6/6/64); setup attempt 3 = `sent=64` + STALL; padding
**1216** STALL (`0xe000404f`); spray identical to E18; overwrite 48
STALL; payload/suffix/zero-len/GETSTATUS `0xe0004051` (ignored);
**final reset**; serial `sz=226` with `PWND:[checkm8]`.

## E19 — RESULT: **PWND:[checkm8] OBTAINED** ✅

User run, device in normal DFU, direct USB-C→Lightning cable,
no hub, no replug. Log `/tmp/tr4mpass-e19.log`. Proof:

```
"USB Serial Number" = "CPID:8000 CPRV:20 CPFM:03 SCEP:01 BDID:02
ECID:<redacted> IBFL:1C SRTG:[iBoot-2234.0.0.3.3] PWND:[checkm8]"
```

`checkm8_verify_pwned: device processed successfully` (two consistent
reads). The device stayed in pwned DFU (no reboot).

**Sequence that actually won** (vs E19 predictions):

| Step | Predicted (E19) | Actual (E19) |
|---|---|---|
| transport | IOKit from stage 1 | ✔ `IOKit active for ALL stages` |
| stage 1 | full gaster cycle | ✔ cycle executed, stage 1 OK |
| inter-stage resets | ×3 + final | ✔ ×3 + final (never a quit-DFU) |
| attempt 3 | `sent=64` + STALL | **`sent=0`** (async artifact, cf. below) |
| padding | 1216 | **1280** — but **STALL (-9)** ✔ |
| overwrite 0x02/0x03+48 | STALL | ✔ STALL (`-9`, "UAF write landed") |
| payload 528/suffix/zero-len/GETSTATUS | `0xe0004051` ignored | ✔ `-7` ignored |
| final reset → serial | PWND | ✔ **PWND:[checkm8]** |

**Scientific reading of the attempt 3 gap**: the IOKit async path
returned `sent=0` (the host counter did not capture the bytes that had
left before the abort — same family of artifact as P5/H1: what the host
reports at the moment of an abort is not what the device ACKed). The
device most likely accepted the 1st 64 B packet **on its side**, then the
padding 1280 STALLed exactly like gaster's 1216. Two readings are
possible, indistinguishable in this run: (a) the padding size has a wide
tolerance (the pad's role is to consume the hole up to the
STALL, the boundary is determined device-side); (b) the bytes
actually ACKed differ from the host count and the alignment happened
anyway. **What is established**: the complete winning recipe =
IOKit end to end + gaster stage 1 cycle + inter-stage reset at
EVERY boundary + final reset (execution trigger) + overwrite
0x02/0x03+48 + post-overwrite results ignored. The exact padding
sizes (1216 vs 1280) and the attempt 3 count are **not**
critical conditions on this machine.

**Overall verdict**: the doc's objective is reached — checkm8 works on
Apple Silicon M-series, direct cable, no hub, no replug, with the
tr4mpass port (byte-exact gaster payloads). E0→E19 closed.

## E20 — Post-exploit transition (pipeline step 3, preparatory phase)

**Next frontier**: after checkm8, `path_a` runs 6 more steps
([3/8] ramdisk → [8/8] verification). Step [3/8] loads iBSS →
iBEC → devicetree → ramdisk via `libirecovery` with **3 USB
reconnections** (DFU 0x1227 → recovery 0x1281 → iBEC) — exactly the area that
palera1n documents as fragile on M-series USB-C ("may require
manual unplugging and replugging **after** checkm8"). A problem
distinct from E19, handled separately.

**Tools built** (in `scripts/`, reusable without an agent):

- `scripts/ipsw_pull.py` — extraction of ONE file from a remote IPSW
  via HTTP Range (reading the ZIP central directory at the end of the file,
  downloading only the targeted entry: ~300 KB instead of 2.2 GB).
  Subcommands: `list`, `url-for <device> <build>`, `probe`
  (extracts + searches for a needle, e.g. `iBoot-`), `fetch`.
- `scripts/detect_ios.sh` — waits for the device in NORMAL mode and writes
  version/build/model to /tmp/ios_{version,build,model}.txt.
- `scripts/ramdisk_test_ibss.sh` — full test A: iOS detection →
  iBSS pull → upload to pwned DFU → PID monitoring 0x1227→0x1281.
  Never touches the checkm8 stage (pre-supposes PWND).

**SRTG discovery (belief correction)**: `SRTG:[iBoot-2234.0.0.3.3]`
is the version of the A9's **BootROM** (SecureROM Tag), NOT that
of iOS — identical on all iPhone 6s. Verified empirically by
extracting the iBSS from 4 IPSWs (9.0 → `iBoot-2817.1.93`, 9.0.2 →
`2817.1.94`, 9.1 → `2817.10.34`, 9.3.5 → `2817.60.2`): never 2234.
⇒ the DFU serial does not allow deducing the iOS version; detection
via `ideviceinfo` (normal mode) is necessary. Note added at the top of the doc.

**Notable iPhone 6s fact**: the IPSW contains TWO iBSS — `iBSS.n71`
(A9 TSMC) and `iBSS.n71m` (A9 Samsung, dual-sourcing). The device's BDID
chooses; the script tries both.

**Next run** (to be done by the user): device currently in
pwned DFU.

```sh
## Pass 1 (consumes the pwned state): the device boots iOS, we read the version
scripts/ramdisk_test_ibss.sh
## ... detection, iBSS download, then "device left pwned DFU"

## Back to DFU + re-pwn (reproducible, cf. E19):
## DFU buttons then ./tr4mpass -v

## Pass 2 (hidden detection): iBSS upload + PID monitoring
scripts/ramdisk_test_ibss.sh
```

Success criterion: final `PASS` (PID 0x1281 observed without replug). In
case of failure, we will know that the post-pwn USB-C transition is the real
M-series subject and we will work on the reconnect (hub recommended by
palera1n, or custom IOKit handling in path_a_ramdisk).

| **E20** | **?** | preparation phase step [3/8]: ipsw_pull/detect_ios/ramdisk_test tools + SRTG≠iOS discovery | tools tested (url-for OK, probe OK on 4 builds) — **iBSS run pending** | run |

| Source | What it establishes |
|---|---|
| DSEC/Habr, "Technical analysis of the checkm8 exploit" (2019) — https://habr.com/en/companies/dsec/articles/472762/ | Complete mechanism: heap feng-shui, UAF, overwrite at 0x5c0 (T8010), callback-chain via gadget `[X0+0x70]`, shellcode. Key transport point: "the device keeps receiving all subsequent SETUPs and places them in its queue" → after a STALL, **the device is healthy**. Arduino PoC: the SETUP+IN cancelled mid-transaction is enough — everything else is normal traffic. |
| ipwndfu `checkm8.py` (axi0mX) — https://github.com/axi0mX/ipwndfu | Full original flow. After the overwrite (`libusb1_no_error_ctrl_transfer(device, 0, 9, 0, 0, overwrite, 100)` — SET_CONFIG!) : payload in DNLOAD 0x800, suffix, zero-len, GETSTATUS ×3, `usb_reset` — **no result verified, no abort, no probe**. |
| palera1n README — https://github.com/palera1n/palera1n | "USB-C port on Apple Silicon Macs may require manual unplugging and replugging of the lightning cable **after** checkm8 exploit" → the documented M-series issue is the step AFTER the exploit, not the exploit. |
| checkra1n, "The state of checkra1n on Apple Silicon Macs" (04/2021) — https://checkra.in/news/2021/04/M1-announcement | The M-series USB stack bug (device crash) only affects **A7, A9X, A10, A10X** — the A9 passes. → a working checkra1n transport exists for the A9 on M-series USB-C. |
| palera1n-c `src/usb_iokit.c` (pongoOS-derived, archived) — https://github.com/palera1n/palera1n-c | The production checkra1n IOKit transport: `(*handle)->ControlRequest(handle, 0, &request)` on `IOUSBInterfaceInterface` (user client interface, pipe 0), `IOUSBDevRequest` **without timeout**, opened via `USBInterfaceOpen`. Differs from gaster's device user client `DeviceRequestTO`. |
| gaster (0x7ff), IOKit backend re-read | `send_usb_control_request` = `DeviceRequestTO` 5 ms, return always-true, payload result ignored; async+abort only for stall/leak/no_leak. That is the device user client path. |

**Overall reading**: everyone agrees on the device protocol
(spray → CLR → overwrite STALL → payload → suffix → zero-len → GETSTATUS ×3
→ reset). The only structuring difference between "tools that work on
M-series" (checkra1n/pongoOS) and "our path that wedges" is the **IOKit user
client used**: interface (ControlRequest, no timeout) vs device
(DeviceRequestTO, TO). That is the subject of experiment E9.

---

---

[← Previous](stage4-breakthrough.md) · [Index](README.md) · [Next →](pongoos-and-kpf.md)
