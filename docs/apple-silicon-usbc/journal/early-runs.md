# Experiment log — early runs (E0–E9)

Each entry: hypothesis → test → result → conclusion. The logs are kept in
`/tmp/tr4mpass-*.log` on the test machine.

## E0 — Initial state (user runs, before this log)

**Observations**: stages 1-3 OK, stage 4 always "complete", never any
PWND. Two variants tested: payload-first (callback never armed) then
gaster-order (overwrite first) with AbortPipeZero + GETSTATE canary
after the STALL.

**Key finding**: `payload offset=0 chunk=528 ret=-7` (DNLOAD 528 B in
timeout) but suffix/zero-len/GETSTATUS OK → the device was going through MANIFEST
**without a payload** → structural false success (§6.1).

## E1 — "gaster-faithful" (strict copy of the gaster flow)

**Hypothesis**: if gaster works on M-series through a hub, its exact sequence
(5 ms everywhere, single-DNLOAD payload ignored, zero abort) must work here.

**Test**: stage 4 rewritten for strict gaster fidelity (§10), 5 ms timeouts,
CLR 193 zeros, single-DNLOAD payload result ignored.

**Result**: immediate and reproducible failure — after the spray, the overwrite
returns `0xe000404f` remapped to `ret=-99` after service re-acquisition
(`overwrite did not STALL (ret=-99)`).

**Conclusions**:
- gaster is **not** a transport reference for direct USB-C Lightning
  (nobody supports that config — we are the ones who have to make it work);
- BUT the failure was partly a **false negative**: `0xe000404f` IS the expected
  STALL, badly remapped (fix §6.2 applied afterwards);
- user decision: **revert** (option A) to the workaround version.

## E2 — Canary without fast cancellations + conditional CLR

**Hypotheses**: (a) the short probes cancelled early are mini-aborts that
re-poison the ring (Run B) → full 5 s probes should recover
like Run A; (b) CLR_STATUS causes the device to re-enumerate and
destroys the UAF state → only send it if the device is not in dfuIDLE.

**Test**: GETSTATE probe 5×5 s; GETSTATE probe before CLR, skip if 0x02.

**Result** (`/tmp/tr4mpass-e2.log`): canary 5/5 `-7` (~25 s) → hypothesis
(a) is **falsified**. The polling GETSTATE already fails (`ret=-7`) → the CLR
is sent anyway → `0xe00002ed` → re-enumeration → stage 4 on a re-enumerated device.

**Conclusions**:
- after the overwrite STALL, **no IN transfer goes through** (500 ms or
  5 s) whereas before the overwrite everything went through;
- `pre-overwrite GETSTATE state=0x02` (dfuIDLE) after the spray+CLR.

## E3 — OUT-first payload (no canary) + aborts after leak/no_leak

**Hypotheses**: (a) after a STALL in the middle of DATA OUT, the ring only
re-arms on an OUT transfer → the payload chunk should go through where the INs
were dying; (b) aborting leak/no_leak right after their timeout "snaps" the
device out of the pending request (like gaster's async) and prevents
the wedge.

**Test**: canary removed, direct 64 B chunks; `AbortPipeZero` after
each synchronous leak/no_leak.

**Result** (`/tmp/tr4mpass-e3.log`): OUT chunk eaten as well (3× `-7`,
guard cuts at 15 s) → (a) **falsified**: direction is not the factor.
The post-timeout aborts change nothing about the final wedge (`0xe00002ed` at CLR).

**Conclusion**: the factor is neither the direction nor the budget; it is
the **abort itself** (all runs with a post-STALL abort die; Run A
without abort is the only one that recovered) — but the abort after synchronous
leak/no_leak is in any case **too late**: on the device side the request stays
pending after the host timeout.

## E5 — Real async+abort transport for the spray (gaster) + 5×5 s canary

**Hypothesis**: gaster's async (DeviceRequestAsyncTO + abort at 1 ms
**while** the request is pending) is the right primitive; our
post-timeout aborts are not equivalent to the in-flight abort.

**Test**: `iokit_usb_ctrl_transfer_async_abort()` (DeviceRequestAsyncTO,
sleep 1 ms, USBDeviceAbortPipeZero, RunLoop for the callback), routed into
leak/no_leak.

**First run** (`/tmp/tr4mpass-e5.log`): the spray loop runs 100
iterations **with no wedge at all** (the service finally survives the loop!) but
`LEAK OK` is never reached: my mapping returned `-7` on
`kIOReturnAborted+wLenDone=0` whereas gaster only looks at `sz == 0`.

**Fix**: gaster semantics on the async return (0 bytes transferred =
success, whatever the status) + simple final gaster CLR (193 zeros, 5 ms,
result ignored, no probe and no pre-abort).

**Second run** (`/tmp/tr4mpass-e5b.log`): spray **OK on the first try**
(`stall+leak+no_leak OK (attempt 1)`), CLR wedges the device once
(`0xe00002ed`, `-99` ignored), service returns in 250 ms, overwrite **STALL
confirmed** … then post-STALL abort (still present in this version) →
ring dead, everything `-7` until the final ResetDevice. No PWND.

**Major conclusions**:
1. **The async+abort transport is THE right primitive**: the spray loop is
   clean, reproducible, first try.
2. **The UAF heap state survives re-enumeration**: after the CLR wedge +
   re-enumeration (250 ms) + service re-acquisition, the overwrite still
   STALLs. (Consistent with gaster: `reset_usb_handle()` does ResetDevice +
   ReEnumerate **between each stage**.) The E2 hypothesis "re-enumeration =
   UAF destroyed" was false — the overwrite STALL is not thereby
   proof of a live UAF (a fresh DFU also STALLs SET_FEATURE), but the
   survival of the heap is now demonstrated positively.
3. The final `0xe00002ed`/CLR is **cosmetic**: re-enumeration is handled
   transparently, no impact on what follows.
4. Only ONE fatal divergence remains versus Run A: **the post-STALL AbortPipeZero**.

## E6 — Removal of the post-STALL abort + long-budget payload

**Hypothesis (Run A decrypted)**: Run A timeline = payload submitted at t≈0
(5 s budget) eaten; suffix submitted at t≈5 s **completed at t≈10 s**. The ring
re-arms at ~10 s and completes any transfer whose budget straddles that moment.
Run A's payload never arrived (timeout expired before the
re-arming) → MANIFEST on an empty buffer → no PWND. **The M-series
adaptation is therefore: payload submitted immediately with a 30 s budget.**
Neither gaster nor anyone else has this case: their hosts have no ingestion
window.

**Test**: no more AbortPipeZero after the overwrite STALL; single-DNLOAD
payload with `USB_EP0_RECOVERY_TIMEOUT_MS = 30000`; everything else (async
spray, gaster CLR) unchanged.

**Result** (`/tmp/tr4mpass-e6.log`): the 30 s budget was indeed applied;
`payload DNLOAD ret=-7` all the same, **and** all the following transfers
(suffix/zero-len/GETSTATUS) dead — unlike Run A where the rest went through.
**Falsified**: the long budget is not enough; without the abort, the window never
closes again (observation ~55 s).

**Final cross-checked matrix**:

| Run | Abort | 1st post-STALL | 2nd | Rest |
|---|---|---|---|---|
| Run A | 1× | IN 5 s eaten | OUT-multi 5 s eaten | **ring back**, MANIFEST OK, no PWND (payload never arrived) |
| E2 | 1× | IN×5 eaten | — | dead |
| E3 | 1× | plain OUT ×3 | — | dead |
| E5b | 1× | OUT-multi 5 s | — | dead |
| E6 | **0** | OUT-multi 30 s | — | dead (~55 s) |

**Conclusion**: the sequence that recovers = **abort + ONE IN eaten + ONE OUT
eaten**, and the eaten payload never arrived at the device (hence Run A's
clean MANIFEST on an empty buffer) → after recovery the payload must be
**re-sent**. E7 implements exactly that sequence.

## E7 — Exact Run A sequence + payload re-send after recovery

**Hypothesis**: faithfully reproduce the only known recovery sequence
(abort → IN 5 s eaten → OUT-multi 5 s eaten), probe GETSTATE
until the ring comes back, then **re-send** the payload in 64 B chunks
(2 ms pacing, 2 fallback rounds with 100 ms between attempts).

**Test**: implemented in `checkm8_patch.c` (Step 2/3); suffix/zero-len/
GETSTATUS unchanged after the re-send.

**Result** (`/tmp/tr4mpass-e7.log`): **falsified.** Two full attempts, same picture:

- `overwrite STALL confirmed` then `AbortPipeZero ret=0x0` (immediate);
- `recovery IN ret=-7` (eaten, 5 s);
- `recovery OUT ret=-7` (eaten, 5 s — the full 528 B, like Run A);
- `recovery probe [1..3/3] ret=-7` → `EP0 ring never recovered` → stage 4
  abandoned (guard at t+25 s).

The Run A prefix (abort + IN eaten + OUT eaten) is therefore reproducible
**but does not re-arm the ring on its own**. Run A recovered at ~10 s when
the next transfer was the **16 B suffix OUT**; E7 dies when the next
transfer is an **IN probe**. New candidate decryption: after
abort + IN eaten + OUT eaten, **OUTs go through but INs do not** — and/or
Run A's recovery was not due to our sequence but to a
self-unblocking of the ROM (~10 s, cf. hypothesis H-watchdog in §11/E8).

**Secondary anomaly observed** (attempt 2): after attempt 1 failed,
the device stayed in state=0x05 then **disappeared** from the bus
(`No such device` ×10 during stage 1) before coming back for
attempt 3. The ResetDevice+ReEnumerate recovery sequence at
the end of a failure deserves a look, but that is secondary.

**In parallel (payload contingency)**: line-by-line comparison of
our `assemble_payload` with gaster — A9 structure (`pwnd[2]` + 11
fields), formula `payload_dest = boot_tramp_end - handler_sz -
sizeof(handler_tail)`, the 15 S8000 offsets (SRTG iBoot-2234.0.0.3.3) and
the tag ` PWND:[checkm8]`: **everything is identical**. The "invalid payload"
hypothesis is ruled out; if the bytes ever arrive, the content is
good.

## E8 — Run A timeline, but it is the payload that completes at t+10

**Prior discovery (re-reading gaster L996-1002, L1230-1236)**:
`dfu_send_data` and the stage patch payload loop send the data in
**several back-to-back DNLOADs** — the ROM **accumulates** the DNLOADs in the
same io_buffer (proof: gaster's setup subtracts what has already been
sent, `config_overwrite_pad - transfer_ret.sz`). For A9: 528 ≤ 2048 →
gaster does a SINGLE 528 B DNLOAD, but successive chunks reconstruct
the same buffer.

**E7 decryption**: after abort + IN eaten + OUT eaten, Run A chained
**OUTs** (suffix 16 B submitted t+5, completed t+10; zero-len; then
GETSTATUS IN OK) whereas E7 put an **IN probe** first → dead.
Candidate rule: after the eaten OUT, the **first transfer that completes
must be an OUT**; INs only go through afterwards.

**E8 hypothesis**: reproduce the Run A timeline but give the
role of "suffix that completes at t+10" to the **payload itself**: after the
Run A prefix (abort + IN 5 s eaten + OUT 528 B 5 s eaten), send the
payload in 64 B chunks (5 s budgets, retry bounded per chunk) WITHOUT any
IN probe. The chunk whose budget straddles t+10 completes (like Run A's
suffix), the ROM accumulates the chunks, the rest flows.

**Test**: replacement of E7's probe/re-send block by the direct chunks
loop (64 B, 2 ms pacing, max 6 tries/chunk, failure if chunk 0
never went through).

**Result** (`/tmp/tr4mpass-e8.log`): **falsified.** After abort + IN
eaten + OUT eaten, the 64 B OUT chunks never complete either
(6 × `-7`, ~30 s observed) → `chunk at 0 never completed`. It is not
the direction nor the budget: **after the overwrite STALL, the
`DeviceRequestTO` path (device user client) is dead whatever we send
to it**, with or without a prior abort. Convergence with Run A:
its "recovery at ~10 s" was probably not a ring recovery
driven by the sequence but a self-unblocking of the host path;
and the payload bytes had not arrived there.

## E9 — Transport change: interface user client (`ControlRequest`)

**Methodological shift (user directive)**: stop guessing
from the source code, go and find the documentation — the DSEC/Habr
research paper, the original ipwndfu code, writeups, and the code of the
tools that WORK on Apple Silicon. Results (§12):

1. **DSEC/Habr (checkm8 technical analysis)**: the `stall` is cancelled
   on the OS side *while it is in the device queue* — afterwards "the
   device keeps receiving all the SETUPs and places them in its
   execution queue". After the overwrite STALL, **the device is
   healthy**; there is NOTHING to "recover" on the device side. The wedge we
   observe is therefore **on the host side**.
2. **ipwndfu (axi0mX, original)**: after the overwrite, it chains
   WITHOUT checking any result: DNLOAD payload 0x800, DNLOAD suffix,
   zero-len, GETSTATUS ×3 (unchecked), then `usb_reset`. Not a
   single abort, not a single probe, not a single re-send.
3. **palera1n README (official)**: "USB-C port on Apple Silicon Macs
   may require manual unplugging and replugging of the lightning cable
   **after checkm8 exploit**" → their M-series problem is the AFTER-
   exploit phase (sending PongoOS to the pwned DFU), not the exploit itself.
4. **checkra1n (official M1 announcement, April 2021)**: the M-series USB
   stack bug that crashes devices only concerns **A7, A9X, A10,
   A10X** — **the A9 is NOT affected** → checkra1n runs the A9
   exploit on M-series USB-C without replug. The transport that
   works exists.
5. **palera1n-c `src/usb_iokit.c` (derived from pongoOS/checkra1n)**: the
   checkra1n production transport on Apple Silicon is NOT
   `DeviceRequestTO` on the device user client (gaster's and
   ours) but **`ControlRequest(handle, 0, &req)` on the interface
   user client** (`IOUSBInterfaceInterface`, pipe 0) with an
   `IOUSBDevRequest` **without timeout**. Different kernel path.
6. **gaster IOKit re-read precisely**: `send_usb_control_request`
   sync = `DeviceRequestTO` 5 ms, return ALWAYS true, result
   ignored by the stage patch (payload NULL). The async (in-flight abort)
   is used ONLY for stall/leak/no_leak. On Intel it works;
   on our M-series, the SAME path wedges after the STALL.

**E9 hypothesis (new)**: the post-STALL EP0 wedge is a property
of the **device user client path** (`IOUSBDeviceInterface320::
DeviceRequestTO`) on Apple Silicon — its TO timeouts and its STALL
handling leave the pipe in a blocking state internally. The
**interface user client path** (`IOUSBInterfaceInterface::ControlRequest`,
pongoOS/checkra1n's, without timeout) does not have that behaviour: it
must go through after the overwrite STALL with no abort and no wait.

**Test**: new transport in `iokit_usb.c`
(`iokit_usb_iface_activate()` + `iokit_usb_ctrl_transfer_iface()`),
wired into stage 4: overwrite via device user client (works,
STALL confirmed on every run), then payload 528 B + suffix + zero-len +
GETSTATUS ×3 via the **interface user client**, with no abort and no probe, then
ResetDevice + ReEnumerate as before.

**Result** (`/tmp/tr4mpass-e9.log`): **falsified as-is, but
new signal.** Opening the interface user client succeeds
(`interface user client opened`), but **every** `ControlRequest`
returns `0xe0004051` (payload, suffix, zero-len, 3× GETSTATUS —
consistent, not a random wedge). Reading: the device user client
stays open at the same time (USBDeviceOpen + SetConfiguration) —
palera1n-c, for its part, opens **ONLY** the interface user client. Probable
exclusive access conflict between the two user clients. **E10** =
run all of stage 4 (or the whole run) through the interface alone:
close the device user client before opening the interface, or open
only the interface from the start.

## Summary table of stage 4 runs

| Run | Abort post-STALL | Post-STALL transport | Ring after STALL | Rest |
|---|---|---|---|---|
| Run A | 1× | sync OUT suffix 16 B → completed ~t+10 s | recovered ~10 s | MANIFEST OK, **no PWND** (payload never arrived) |
| Run B | 2× | sync | dead | everything `-7` |
| canary | 1× | sync IN | dead (IN 500 ms then 5 s ×7) | everything `-7` |
| E2 | 1× | sync IN×5 | dead | everything `-7` |
| E3 | 1× | sync OUT chunks | dead (plain OUT ×3) | guard cuts at 15 s |
| E5b | 1× | sync single-DNLOAD | dead | everything `-7` |
| E6 | 0 | sync single-DNLOAD 30 s | dead (~55 s) | everything `-7` |
| E7 | 1× | sync IN eaten, OUT eaten, **IN probes** | dead (probes `-7` ×3) | guard cuts at t+25 s |
| E8 | 1× | sync IN eaten, OUT eaten, **64 B OUT chunks** | dead (chunks `-7` ×6) | guard cuts at ~30 s |
| **E9** | **0** | **interface user client `ControlRequest`** (device user client still open) | interface opened but everything `0xe0004051` | E10: interface ALONE (close the device client) |
| **E10** | **0** | **whole stage 4 via libusb** after the IOKit spray | clean `-9` overwrite then **everything `-7`**; SET_FEATURE halts host EP0 | E11: overwrite form `0x00/0x00` |
| **E11** | **0** | **overwrite `0x00/0x00`** (openra1n), rest gaster-exact DeviceRequestTO | full sequence: overwrite `-9`, payload **528 OK**, suffix 16, zero-len 0, MANIFEST 0x06→0x07→0x08 — but **no PWND** (silent callback or overwrite never landed?) | E11b: log the STALL `wLenDone` |
| **E11b** | **?** | same as E11 + STALL `wLenDone` logged | `wLenDone=0` at the STALL (ambiguous: IOKit may not update the field on STALL) | E12: wIndex 0x80→0x00 |
| **E12** | **0** | **exact openra1n/ipwndfu overwrite** (0x00,0,0,**0x00**,48) | signature identical to E11 (no PWND); SetConfiguration(1) confirmed in gaster L335 + palera1n L143 | E13: skip stage 3 CLR_STATUS (env `TR4MPASS_SKIP_CLRSTATUS=1`) |
| **E13** | **?** | **spray without CLR_STATUS** (no `0xe00002ed` wedge/re-enumeration before the overwrite) | payload **dies** `-7`×6 on the original handle after the overwrite STALL → ANY STALL wedges the current handle (corrected E10 reading) | E14: rebuild the handle between overwrite and payload |
| **E14** | **?** | E13 + **IOKit handle rebuild** between overwrite and payload | payload `-7`×6 — re-activation reveals that **the DEVICE re-enumerates** after the STALL (not just the host) | E15/E16 |
| **E15** | — | `shim.c` libopenra1n (interface client lead) | **eliminated**: it calls `DeviceRequestTO`, the same transport as ours | — |
| **E16** | **✅** | **gaster 0x7ff compiled and run** (reference implementation, official A9 payloads) | **PWND:[checkm8] OBTAINED** — direct cable, no hub, no replug → our port has a precise defect | E16bis: structural diff → E17: gaster trace |
| **E16bis** | — | diff our stage 3/4 ↔ winning gaster (code + config dump) | 0x8000 table + 528 payload identical; 2 diffs: strict stage separation (ResetDevice+ReEnum+fresh handle); overwrite **0x02/0x03** with data 48 (rule 3 falsified) | E17 |
| **E17** | **?** | request-by-request trace of the winning gaster (env `GASTER_TRACE`) | *pending: device to be put back into normal DFU* | E18 |
| **E18** | **⚠️** | gaster-exact port (overwrite 0x02/0x03, no rebuild, inter-stage reset, 5 ms timeouts) | overwrite STALL **then callback EXECUTED** (device rebooted out of DFU at bus reset — never seen before); payload data phase never left (`-7`×6, wLenDone=0); blobs byte-exact upstream | E17: winning gaster trace |
| **E19** | **✅** | P5=H1 + P3 port + gaster-exact orchestration (IOKit from stage 1, reset ×3 + final, IOKit verify) | **PWND:[checkm8]** — direct cable, no hub, no replug; attempt 3 `sent=0`/pad 1280 (artefacts) but correct STALLs everywhere; device left in pwned DFU | E20 |
| **E20** | **?** | preparation of step [3/8]: `scripts/{ipsw_pull.py,detect_ios.sh,ramdisk_test_ibss.sh}` + discovery SRTG=BootROM (≠ iOS) | tools tested — **iBSS run pending** | DFU→recovery transition run |

## E20b — Analysis of palera1n v3.0.0-beta.3 (clone /tmp/palera1n-check)

**Why the clone**: check whether palera1n contains ready-made images
(iBSS/iBEC/ramdisk/pongo) that could be reused. **Answer: NO** —
`Pongo.bin` and `ramdisk.dmg` are **CI artefacts**
(`.github/workflows/build.yml`, jobs `build-pongo`/`build-ramdisk`),
not in the repo. But reading the source reveals an **architectural
choice different from ours**, with a direct impact on the
M-series USB risk.

**palera1n architecture = pongoOS-as-payload (1 single reconnection)**:

1. classic checkm8 (libopenra1n, 1stparty/, MIT) up to the patch;
2. the stage 4 payload is NOT a small shellcode: it is
   **pongoOS** compressed with LZ4, encapsulated by `generate_stage1`
   (stage1.c) with `lz4dec.bin` (500 B) as the decompressor;
3. the overwrite triggers: lz4dec → pongoOS → the device re-enumerates
   **PID 0x4141 (pongoOS)** — no 0x1281 recovery mode, no iBSS,
   no iBEC, no IPSW devicetree;
4. EVERYTHING else (kpf, ramdisk, boot, shell) goes through the pongoOS
   shell over USB (`pongo_helper.c`, 295 lines: commands + file
   upload + `bootx` from pongo).

**For the A9 (0x8000)**: `yolo_s8000.bin` (2780 B, YoloDFU stage-1
loader) is EMBEDDED in the repo; `generate_stage1` takes the
**non-wnx** path for A9 (A9X/A10/A10X/A11/T2 are wnx — not us) →
presize 0xc0, simpler. Compatible with our CPID.

**Important detail**: palera1n **REFUSES pwned DFU devices**
(exploit.c L381-387 "Detected pwned DFU mode device, please enter
normal DFU mode") — its flow does pwn+pongo in a single pass.
Our 2-step flow (pwn then separate post-exploit) remains valid
but must drive pongoOS after our own pwn.

**Comparison of the two routes for path_a**:

| | IPSW route (our current path_a) | pongoOS route (palera1n) |
|---|---|---|
| USB reconnections | **3** (DFU→recovery→iBEC→recovery) | **1** (DFU→pongoOS) |
| M-series USB-C risk | high (palera1n documented area) | minimal (same mechanics as the pwn) |
| images required | iBSS+iBEC+devicetree+ramdisk+trustcache (IPSW + preprocessing) | Pongo.bin (CI artifact, to obtain/build) + custom ramdisk |
| code to write | 0 (path_a_ramdisk.c already fully written) | pongoOS client (~300 lines, pongo_helper.c model) + stage1 in checkm8_patch |
| ramdisk | IPSW extraction + kernel patches + mobileactivationd replacement | palera1n custom ramdisk (already contains what is needed, adapt to our goal) |

**Chosen strategy**: test A (iBSS) remains the **discriminating
measurement** — if the DFU→recovery transition goes through without replug, the
IPSW route (already written) is the shortest. If it fails, pivot to the
pongoOS route (a single transition, the same one that already works in E19).
Test A costs 2 minutes and decides.

| **E20b** | **ℹ️** | palera1n v3.0.0-beta.3 analysis: pongoOS route (1 reconnection) vs IPSW route (3 reconnections); yolo_s8000.bin embedded; Pongo.bin/ramdisk.dmg = CI artefacts | decision deferred to the result of test A | iBSS run |

**E20b-addendum — palera1n does NO activation at all** (exhaustive grep
over the whole tree: 0 occurrence of "activation", "hacktivation",
"setup.app", "mobileactivation"). What palera1n actually does,
in 3 layers:

| Layer | Where | Persists across reboot |
|---|---|---|
| kernel patches (KPF: AMFI, sandbox, signatures) | **RAM only** | ❌ no |
| bootstrap (Sileo, tweaks) | 2nd APFS volume "fakefs" (iOS 15+) or `/var/jb` | ✅ yes |
| palera1nLoader app | `/Applications` | ✅ yes |

Actual sequence (`pongo_helper.c::send_full_pongo_jailbreak`):
`fuse lock` → `sep auto` → upload KPF → `modload` → upload ramdisk →
`ramdisk` → upload binpack → `overlay` → `xargs <boot_args>` →
`bootx`. **No modification of iOS on disk**: the real kernel
boots with patches applied in RAM. Hence the semi-tethered
nature (re-run the tool after each reboot).

⇒ For our objective: a device that is **not activated** (stuck on Setup)
stays that way after palera1n. palera1n provides neither an activation
record, nor a mobileactivationd replacement, nor a deletescript, nor
preservation of Signal. **The only thing worth borrowing: the USB transport** (1 transition
instead of 3). The whole [4/8]→[8/8] pipeline remains tr4mpass code.

## M-series rules validated empirically (current state of the art)

1. Spray: **async transport + in-flight abort** (1 ms), never synchronous;
   success semantics = `sz == 0`, not the completion status.
2. Final CLR_STATUS: 193 zeros, 5 ms, **result ignored**; the `0xe00002ed`
   wedge + the re-enumeration it causes is inconsequential
   (gaster does ResetDevice+ReEnumerate after EVERY stage — gaster_ref
   L1285 — and the device-side heap survives; confirmed: the overwrite
   STALL comes back after the re-enumeration).
3. **The request that kills host EP0 is SET_FEATURE(ENDPOINT_HALT)
   (0x02/0x03)** — it sets the halt bit on EP0-IN on the controller side;
   afterwards EVERY transfer dies in timeout (`-7`), via libusb as
   via DeviceRequestTO, direction and budget irrelevant (E2..E8,
   E10). A simple rejection (`0x00/0x00`) STALLs WITHOUT setting the halt:
   EP0 survives (stage 2: `-9` then `ret=64` OK on the same handle).
   ⇒ The overwrite must use the **0x00/0x00** form (ipwndfu,
   openra1n), not gaster's 0x02/0x03 form.
4. The payload content is NOT the problem: `assemble_payload` is
   identical byte-for-byte in structure and offsets to gaster (§11/E7).

---

---

[← Previous](README.md) · [Index](README.md) · [Next →](stage4-breakthrough.md)
