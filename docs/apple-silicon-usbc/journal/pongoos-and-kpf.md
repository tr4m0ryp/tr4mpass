# Device model, pongoOS and KPF (E20–E33)

## E20 — The device is NOT an iPhone 6s

- DFU serial: `CPID:8000 BDID:02` → libirecovery table L195-196:
  `{ "iPhone8,4", "n69uap", 0x02, 0x8000, "iPhone SE (1st gen)" }`.
  The "iPhone 6s" of the research doc was wrong (n71 = BDID 0x04).
- Verified live after iOS boot: `ideviceinfo` → ProductType **iPhone8,4**,
  HardwareModel **N69uAP**, **iOS 14.6 / build 18F72**.
- Consequence: the n71 images previously fetched were for the wrong
  board → explains silent no-boot #1. Images re-fetched for
  18F72 (iBSS/iBEC/DeviceTree n69u + ramdisk 018-18125-090.dmg Erase
  + trustcache), env.sh rewritten. n71 artifacts deleted.

## E21 — `irecovery -f` does NOT boot a payload on gaster pwned DFU

- iBSS.n69u (wrong board first, then right board) uploaded to 100%,
  rc=0, 3 times. No DFU→recovery transition.
- Verified in libirecovery.c (upstream source): `-f` = DNLOAD 0x800
  chunks → ZLP (wValue=packets) → 2× GETSTATUS → `irecv_reset`.
  This is the **stock DFU** sequence; on the gaster pwned runtime, the
  `dfu_handle_request` hook does not interpret this dance as a boot
  order (no 16-byte suffix, wrong state walk).

## E22 — Correct boot sequence = gaster's (dfu_boot)

Tool `tools/dfu_boot.c` reproducing gaster `dfu_send_data` +
`dfu_set_state_wait_reset`: DNLOAD chunks 0x800 → 16-zero suffix →
ZLP → GETSTATUS ×3 (states 6→7→8, observed **exactly**) →
`irecv_reset`. The state walk succeeds (the DFU hook runs) but the
device re-enters pwned DFU on reset: **the uploaded image was never
executed**. Two causes identified later (E23):

1. libopenra1n's (palera1n) `0x21/4` trigger is a hook of
   **their** payload, not gaster's — sent, ignored (r=-0, no-op).
2. The image sent was the **GID-encrypted** .im4p: even if the hook
   had copied it and jumped to it, it is encrypted garbage. The SRTG
   iBoot-2234.0.0.3.3 reappears on reset = intact payload that
   re-arms pwned DFU (normal behaviour).

## E23 — exec channel: transport OK, result always empty

- gaster's AUTHENTIC `decrypt_kbag` (compiled from master
  0x7ff/gaster, payloads byte-identical to ours — verified by
  extracting/comparing the two embedded binaries) fails the same way:
  GET 0xA1/2 wValue=0xFFFF → timeout (libirecovery `-11`) or
  `0xe0004051` (our E9 IOKit interface transport).
- Exact gaster sequence reproduced (DNLOAD data → suffix 16 → ZLP
  → states 6→7→8 → GET on the SAME handle, NO reset): identical.
- GETSTATUS/plain GET 0xA1/2 wValue=0: timeout too — yet the
  `_request_done` hook answers ANY 0xA1/2 (0x80-byte pass-through).
  ⇒ Symptom: the `handle_interface_request` branch NEVER executes,
  or `insecure_memory_base` does not contain what we think.
- Main lead: our E19 installed the payload with the modified
  stage-4 recipe (overwrite 0x02/0x03+48B, intermediate resets ×3).
  The DFU hook (chain `dfu_handle_request` → `_main` →
  `handle_interface_request`) works for standard DFU requests,
  but if the **tail** of the handler (exec_magic, done_magic,
  insecure_memory_base) is corrupted or points elsewhere, the
  exec branch (0x2A1 / 0xFFFF) never matches → timeout. Note: the
  PWND: read works (string descriptor), the state walk too — everything
  passing through the beginning of the hook works; everything that
  depends on the tail does not answer.
- Discriminating test scheduled (E24): blank DFU → STOCK `gaster pwn`
  (no modification) → `decrypt_kbag` immediately. If OK,
  our stage-4 recipe is the culprit (revert to the exact gaster
  overwrite 0x00/0x00, without extra intermediate resets);
  if KO, the problem is transport/platform (IOUSBHost = plan B).

## Journal

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E20** | **🛑** | device = iPhone8,4/n69u (BDID 0x02 + CPID 0x8000), NOT 6s/n71; real iOS 14.6/18F72 | n71 images discarded, correct 18F72 re-fetch | boot gate with decrypted iBSS.n69u |
| **E21** | **🛑** | `irecovery -f` (stock sequence) boots nothing on gaster pwned DFU (3 uploads at 100%, 0 transition) | dedicated dfu_boot tool required | superseded |
| **E22** | **🛑** | exact gaster sequence (suffix+states 6→7→8+reset): states OK, image never executed; 0x21/4 trigger = palera1n hook, no-op on gaster | cause = encrypted image + execution never triggered | GID decryption required |
| **E23** | **🛑** | exec channel mute even with stock gaster on the existing pwn; pass-through GET 0xA1/2 wValue=0 times out too | DFU hook partially functional, exec/tail branch suspected corrupted by our E19 stage-4 recipe | E24: stock pwn → decrypt_kbag → boot |

## E24 — Stock gaster discriminator: OUR E19 recipe was corrupting the exec channel

Protocol: blank DFU → STOCK `gaster pwn` (no mod) →
`decrypt_kbag` → `decrypt iBSS`.

| Step | Result |
|---|---|
| gaster pwn stock (4 stages) | ✅ all `ret: true` |
| `decrypt_kbag` (kbag 18F72 extracted from the iBSS) | ✅ **IV/key decrypted and returned** |
| `decrypt iBSS.n69u` | ✅ 239,296 B binary, valid ARM64 (mov x16/x17…) |

⇒ The transport, the device and the exec protocol are fine. It was the
modified stage-4 recipe of E19 (overwrite 0x02/0x03, resets ×3) that
was corrupting the **tail** of the handler (`exec_magic`/`insecure_memory_base`)
— everything passing through the beginning of the hook worked (PWND:, DFU states),
everything depending on the tail never answered (exec channel mute).

## E25 — boot-on-reset is DISABLED by design in the gaster payload

`payload_A9.S::_main` line 2: `str xzr, [dfu_handle_bus_reset]` —
gaster sets the ROM's bus-reset callback to NULL **on purpose**: that is
why PWND: survives resets… and why NO "DNLOAD then reset" sequence
can boot an image after a gaster pwn (the image may have been copied
to memory, but the ROM's boot path is dead). The E21/E22 uploads could
therefore never boot, decrypted images or not. libopenra1n's `0x21/4`
sequence is a hook of THEIR payload (yolo), not gaster's.

Boot after pwn-gaster goes through a **stage2**: a second payload
that re-enumerates its own DFU device with a live boot-on-reset
(ra1npoc `common.c::payload_stage2` + `connect_to_stage2`:
DNLOAD → ZLP → 3×GETSTATUS → the image boots, no reset hack).

## E26 — PONGOOS ROUTE PROVEN on M4 USB-C (end of test A)

`ra1npoc` (kok3shidoll, checkra1n payloads dumped from v0.12.4, MIT
licence) compiled for macOS (clang + IOKit), launched on BLANK DFU:

```
checkm8_arm64: set_global_state OK, heap_occupation OK, checkmate!
sending stage2 payload → connecting to stage2 → **connected to Stage2**
sending pongoOS → BOOTED? rc=0
```

Then the device **booted iOS normally** (pongoOS auto-boot of the
kernel by default: the NO_AUTOBOOT flag is only set for S8001).
Verified: `ideviceinfo` → iPhone8,4, 14.6, Unactivated. All
USB transitions survived (DFU→stage2→pongoOS→iOS).

**Consequences**:
1. Test A (iBSS route) is moot: the pongoOS route (E20b
   "pongo option", 1 transition) is PROVEN on this machine.
2. The pwn via our E19 C code remains useful (skip-if-pwned) but the
   boot goes through the ra1npoc loader (or a port of its stage2 logic).
3. Next step: keep pongoOS alive — patch ra1npoc's S8000 case to
   set `NO_AUTOBOOT` (like S8001), then `pongoterm`
   to load custom KPF/ramdisk and `bootx` (steps [4/8]→[5/8]).

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E24** | **✅** | stock gaster: pwn + decrypt_kbag + decrypt iBSS OK on M4 | our E19 stage-4 recipe was the culprit (corrupted tail) | realign stage-4 to stock gaster if we re-pwn ourselves |
| **E25** | **ℹ️** | gaster payload neutralises boot-on-reset (payload_A9.S); boot = via stage2 | definitive explanation of the E21/E22 no-boots | — |
| **E26** | **✅✅** | ra1npoc s8000: exploit → stage2 → pongoOS → full iOS boot on M4 USB-C | PONGOOS ROUTE retained; test A obsolete | NO_AUTOBOOT + pongoterm |

## E27 — s8000 full blob: pongoOS runs but auto-boots the kernel

Test with the `pongo_shell` kerninfo set (KPF_FLAGS_PTR=0x158018,
`checkrain_option_pongo_shell`): ra1npoc s8000 → stage2 connected →
pongoOS sent → **the device screen displays the KPF patching
messages** (user visual confirmation) then "recfg done /
rebooting" → device gone into recovery (PID 0x12A8, serial=UDID).

Findings:
1. **pongoOS lives**: "patched" on screen = xnu_hook/KPF at work.
   The pongo shell booted and worked — it is a patched checkra1n pongoOS,
   not mainline (which has NO autoboot: `while(!gBootFlag)`
   waits indefinitely, entry.c L210).
2. The blob's `pongo_shell` flag did NOT stop autoboot: the checkra1n s8000
   stage2 builds the autoboot block (magic `autoboot` placed
   at 0x418e00000, mm.c L802) independently of the blob's kerninfo.
3. ra1npoc's s8001 path (main.c L386-392) is the solution proven
   in the code itself: `payload.pongoOS_len = 0x3F2F8` (exactly
   pongoOS.bin, 258,808 B) + `NO_AUTOBOOT` → no KPF/RDSK in
   the image → live pongoOS shell, then driven by pongoterm
   (rdsk → "ramdisk" → kpf → "modload" → "xargs" → "bootx").
4. E28 = s8000 with the s8001 recipe. If the shell stays alive, the
   interaction window is unlimited (pongoOS has no timeout).

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E27** | **ℹ️** | s8000 pongoOS lives (KPF "patched" on screen) but stage2 auto-boots despite pongo_shell | window too short via full blob; s8001 recipe = live shell | E28: truncate 0x3F2F8 + interactive pongoterm |

## E28 — PONGOOS SHELL ALIVE AND DRIVEABLE (s8000, s8001 recipe)

Patched ra1npoc (s8000: `pongoOS_len = 0x3F2F8` + NO_AUTOBOOT, cf.
E27) on blank DFU:

```
checkmate! → stage2 connected → pongoOS sent → BOOTED? rc=0
USB: serial "pongoOS / checkra1n 2.5.1-9234e72f", PID 0x4141
```

pongoterm (IOKit, hotplug PID 0x4141): **[Connected]**, `pongoOS>`
prompt active. Commands executed successfully:
- `help` → full set: `ramdisk`, `modload`, `xargs`, `bootx`,
  `bootux`, `loadx`, `sep`, `dt`, `spawn`...
- `xargs` → "xnu boot arg cmdline: []"
- `lsdev` → device tree (system-vol, data-vol, baseband-vol...)

The interaction window is **unlimited** (no timeout, no
autoboot without an `autoboot` block). Terminating without bootx = the device
stays in pongoOS; Power+Home to get out.

**Pipeline activated**: step [4/8] (execution environment) =
pongoOS shell. The ramdisk steps (`ramdisk` cmd + bulk upload 0x21/1
wValue=size then bulk EP2, cf. pongoterm ra1npoc-mode) are
reachable without replug. The KPF will be loaded via `modload` (separate
upload), not embedded in the blob.

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E28** | **✅✅** | live pongoOS shell + unlimited interaction (xargs/lsdev OK) | step [4/8] PROVEN on M4 USB-C | upload ramdisk 18F72 + KPF + bootx; integrate ra1npoc/pongoterm into the repo |
## E29 — 105 MB UPLOAD INTO THE PONGO SHELL (18F72 ramdisk in memory)

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E29** | **✅✅** | Erase ramdisk 018-18125-090.dmg (105,367,579 bytes) uploaded into `loader_xfer_recv_data` via `tools/pongo_upload`, then consumed by the `ramdisk` command (clean reply, no error) | step [4/8] ramdisk IN MEMORY — only bootx remains | bootx (iOS must start up with the ramdisk) |

Technical details (all verified live, device in shell 2.5.1 PID 0x4141):

1. **Control transport = user client interface** (`IOUSBInterfaceInterface245`,
   `ControlRequest` pipe 0), not the device client. The device client stalls on
   0x21 requests (E9 rediscovered). `cmd` works: `0x21/3 "<cmd>\n"`.
2. **Upload init**: `0x21/1 wLength=4` with the size in data-out
   (`reallocate_loader_xfer_data`: round-up 512, ≤128 MB, primes the EP2 OUT DMA).
   `wLength=0` = fixed 1 MB variant (usbloader.c:107).
3. **Stall 0xe000404f (kIOUSBPipeStalled)**: any 0x21/1 request stalls if
   `usbloader_is_waiting_xfer==1` (EP2 DMA pending). Recovery proven:
   `drain` = write 1 MB of junk to pipe 2 → DMA completed → shell OK again.
4. **Large M4 upload**: WritePipe 16 KiB reaches 99% (104,873,984) then blocks;
   a drain from a NEW connection completes the DMA exactly
   (104,873,984 + 493,624 = 105,367,608 = round-up size). Robust fix:
   `WritePipeTO` (5 s) + `ClearPipeStallBothEnds` + interface reopen;
   the full run then goes through without a hitch.
5. **Padding**: the DMA size is round-up 512; pad = 512 − (sz mod 512);
   105,367,579 → pad 485 (my first "29" arithmetic was wrong).
   Upload auto-padded; commands `pad <n>` / `query` (read stdout 0xA1/1
   wLength=512) / `cmdq` (cmd + deterministic reply) added.
6. **`ramdisk` diagnostic**: "please upload a ramdisk" = counter
   `loader_xfer_recv_count==0` (buffer already consumed or upload not completed),
   NOT a shell failure. Second call after consumption → expected message.

Tools: `tools/pongo_upload` (upload|cmd|cmdq|query|drain|pad).
Script: `scripts/pongo_load_ramdisk.sh` (deterministic cmdq).

## E30 — BOOTX WITH KPF: THE DEVICE BOOTS, NO PANIC

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E30** | **✅✅** | bootx with KPF modloaded → iOS boots OK, device alive (iPhone8,4, 14.6, Unactivated, ideviceinfo responds) | E29 panicked because KPF was absent; with KPF the patched kernel accepts the boot | full scripted reproducible run; activation afterwards |

Details:

1. **Cause of the E29 panic confirmed**: bootx with ramdisk but without KPF
   → kernel not patched → panic. The checkra1n KPF (Mach-O cffaedfe,
   0x15648 bytes) lives at 0x3F308 in the composed blob
   `dumpfiles/pongoOS` (1,414,476 bytes); extracted by script in
   build_ra1npoc.sh → `images/kpf_checkra1n.bin`.
2. **Full scripted run**: ra1npoc (fresh DFU) → shell →
   `pongo_load_ramdisk.sh --kpf kpf_checkra1n.bin` → upload ramdisk (105 MB,
   pad 485) → `ramdisk` → upload KPF (87,624, pad 440) → `modload`
   (KPF banner on screen, seen by the user) → `bootx` → boot OK.
3. **M4 USB noise**: the KPF upload stalled at 0 (0xe0004051
   transaction timeout) then recovered via ClearPipeStallBothEnds; the
   `modload` reply timed out on the host side (shell busy) and the
   `bootx` write got 0xe00002ed — but the boot went through anyway:
   these errors are host-side, the device carries on.
4. The ramdisk is in RAM; no persistent change. Unattended
   reboot = back to normal iOS.

Final chain proven: fresh DFU → checkm8 (ra1npoc) → stage2 → pongoOS
shell → 18F72 ramdisk in memory + KPF → bootx → patched iOS alive.

## E31 — REAL-WORLD ACTIVATION: THE DEVICE IS ACTIVATION-LOCKED

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E31** | **ℹ️** | ideviceactivation (libimobiledevice master, local build) → Apple server: "Activation Lock — linked to an Apple account s●●●●●@icloud.com" | normal activation IMPOSSIBLE without the linked account's iCloud credentials; the tr4mpass route (patched MAD + deletescript) is the only one left IF there is no access to the account | user decision: are credentials available? |

Details:

1. `ideviceactivation` absent from Homebrew; built from
   libimobiledevice/libideviceactivation master (autogen + make,
   installed into ~/.local/opt/ideviceactivation).
2. `ideviceactivation activate` (device in normal mode, Unactivated):
   the server replies Activation Lock + asks for email/password
   interactively — non-interactive failure expected, the root cause is the lock.
3. Direct consequence: steps [5/8]-[6/8] of Path A
   (replacing mobileactivationd via ramdisk) now require
   a boot with boot-args rd=md0 + deletescript — the E30 ramdisk was
   in memory but unused (no xargs rd=md0; documented caveat).

## E32 — CUSTOM SSH RAMDISK: BUILT AND VERIFIED (host-side)

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E32** | **✅✅** | `scripts/build_ssh_rd.sh` produces `images/ramdisk_ssh.dmg` (IM4P rdsk, 105,367,578 bytes < 128 MiB cap), HFSX volume verified: dropbear + LaunchDaemon + bin/ kit present | bypass route unblocked: no more need for WKEY (the stock rdsk is ALREADY DECRYPTED) nor for dora2 (dead) -- BSD-3 kit verygenericname/SSHRD_Script vendored | boot the SSH rdsk: DFU -> ra1npoc -> pongo_load_ramdisk --kpf --bootargs rd=md0 -> ssh root@localhost -p 2222 |

Key findings:

1. **The 18F72 rdsk is in the clear**: IM4P(rdsk) -> payload = raw HFSX
   volume (HX @0x400), low entropy, mounts directly via hdiutil after
   `ipsw img4 im4p extract`. No WKEY/GID needed (the plan
   assumed the opposite -- option C simplified by one notch).
2. **SSHRD_Script kit** (verygenericname, BSD-3, 645*) vendored into
   `tools/sshtars/`: dropbear (blank-password auth verified via strings) +
   host keys + bash + launchctl + img4tool/ldid + restored_external
   (which execls dropbear on the USB mux and launches mount_filesystems).
3. **The kit supplies the missing dylibs of the stock rdsk** (libncurses,
   libresolv absent from the Apple rdsk; the kit embeds them in usr/lib).
4. **Trim required**: volume 100.4 MiB with 27 MiB free; kit trimmed to
   ~20 MiB (no locale/terminfo/nano/magic/TrollStore). hdiutil CANNOT
   resize a raw HFSX volume (no DMG wrapper) --
   so no grow, the trim is mandatory.
5. **hdiutil requires the .dmg extension** to attach a raw HFS
   (rd.hfs refuses, rd.dmg OK); an old read-only mount of the
   parent volume blocks new attaches (ejected).
6. **Script bug fixed**: the modified volume is rd.dmg (copy), not
   rd.hfs (original) -- the IM4P wrap must take rd.dmg; the verify
   cmp rd.dmg <-> extract(IM4P) is byte-identical, dropbear present.
7. **The kit's restored_external**: strings verified -- execl
   /usr/local/bin/dropbear (USB mux, micro_inetd) + /bin/bash
   /usr/bin/mount_filesystems; LaunchDaemon com.tr4mpass.dropbear
   RunAtLoad added (the rdsk's launchd starts it at boot).

## E32b — USB PANICS "send 4": ROOT CAUSE (pongo client protocol)

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E32b** | **✅✅** | 2 pongoOS `usb bug send 4` panics (0x73656e642034) caused by the host client: (1) stdin writes (0x21/3) while a command was still running, (2) flow 0x21/4 wValue=0 "stall next stdin" + 5s timeout that CANCELS a half-accepted EP0 transfer -> desync of pongo's USB state machine | NEVER write stdin while a command is running; NEVER use 0x21/4 wValue=0; official flow = poll 0xA1/2 (async completion status, 1 byte, 0=idle) before each write + stdout via 0xA1/1; `run_cmd` = plain 0x21/3 (what pongoterm sends) | `tools/pongo_upload.c` rewritten: `cmd_status` (0xA1/2) + `cmdq <cmd> [expect]` (poll idle -> drain stdout -> 0x21/3 -> stream stdout until expect, rc 0/2) |

Key findings:

1. **pongoOS client protocol** (usbloader.c + pongoterm):
   `0x21/1 wLength=0` 1MB upload init; `0x21/1 wLength=4` =
   `reallocate_loader_xfer_data` (size rounded to 512, DMA armed for
   exactly that size — pad the tail, e.g. 105367579 -> pad 485);
   `0x21/3` = write shell stdin; `0xA1/1` = read stdout;
   `0xA1/2` = command_in_progress (0 = idle). Control transfers
   go through the user-client **interface** (IOUSBInterfaceInterface245),
   not the device client.
2. The panic mechanism: a host timeout on a stalled EP0 write
   cancels a half-accepted transfer -> pongo derails; the subsequent
   rewrites hit a broken pipe until the USB core panics. checkra1n
   clients NEVER cancel a stalled EP0 write: they
   poll the status and only write when idle.
3. `xargs rd=md0 debug=0x2014e -v wdt=-1` confirmed with the
   **tiny** expect `set xnu boot arg cmdline` (the device replies in
   lowercase; the mixed-case expect never matched -> the script
   retried in a loop).
4. After `bootx` the 0xA1 requests return 0xe00002c0 (device already
   rebooting) — expected, not an error.

## E32c — 64 MiB STATIC REGION CAP + PANIC CONFIRMED LIVE

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E32c** | **✅✅** | Live on device: bootx -> preboot hook -> APFS mount -> KPF patches the DISK kernel (~40 patches, "Found ramdisk" -> ramdisk patchset applied) -> `panic: ran out of static region` AFTER the complete patchset | pongoOS `alloc_static` has a hardcap of **64 MiB** (mm.c: `alloc_static_hardcap` = base + 64 MiB; `xnu_loadrd` copies the ENTIRE ramdisk there, xnu.c:1304, so that it survives into xnu). The 105 MB rdsk will NEVER boot -> slim to ≤ ~56 MiB mandatory | build_ssh_rd.sh rewritten in slim mode (see E33) |

Details:

1. Source: `/Users/mleroy/.local/share/opencode/tool-output/pongoOS-src/src/kernel/mm.c`
   (alloc_static, alloc_static_hardcap) and `src/drivers/xnu/xnu.c:1304`
   (`rd_static_buf = alloc_static(ramdisk_size)`).
2. The boot kernel comes from the device's disk (APFS mount by pongo's
   preboot hook) and is patched by KPF in place — no need
   to upload a kernelcache (kernelcache.release.iphone8b remains
   available but unused).
3. The panic happens AFTER "KPF: Applied patchset" — the whole
   exec path is fine; only the memory budget is missing.

## E33 — SLIM SSH RAMDISK: 44 MiB, WITHIN THE CAP

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E33** | **✅✅** | `build_ssh_rd.sh` slim: kit trimmed 20 MiB -> delete `/usr/standalone` (27 MB peripheral firmware), PrivateFrameworks, Extensions, every Framework except CoreFoundation+IOKit, FDR/FeatureFlags, seputil/SavageUtil/DumpPanic/diskimagesiod, sbin restore-only, purge `/usr/lib` (keeping the otool closure of the surviving binaries) -> 40,444 KiB used -> volume rebuilt to **44 MiB** (SSHRD_Script recipe hdiutil create -srcfolder -copyuid root) -> IM4P(rdsk) **46,137,370 bytes** | 64 MiB cap respected with ~20 MiB of headroom (devicetree + misc); round-trip verify: cmp byte-identical + dropbear/restored_external/stock plist/mount_filesystems/bash present + dylib closure re-verified on the slim fs | device boot: DFU -> ra1npoc -> pongo_load_ramdisk --kpf --bootargs rd=md0 -> bootx -> SSH iproxy 2222 |

Key findings:

1. **Redundant LaunchDaemon removed**: the STOCK plist
   `com.apple.restored_external.plist` already launches
   `/usr/local/bin/restored_external` — a path that the kit tarball
   OVERWRITES with its own restored_external (execl dropbear + mount).
   The com.tr4mpass.dropbear plist added in E32 was therefore useless.
2. **The /usr/lib purge must only touch STOCK libs**: snapshot
   `ls usr/lib` BEFORE copying the kit; the kit provides libncurses/
   libresolv/libimg4tool/libplist/libssl/crypto/libgeneral/libiosexec
   — a first purge pass had wiped them (the post-slim verify
   caught it: ioreg:libncurses.5.4, sftp-server:libresolv.9,
   img4tool:*).
3. **otool closure of the survivors**: libSystem(+sub-libs+dyld),
   libobjc.A (launchd), libutil (mount_apfs), libz/libiconv/liblzma/
   libbz2/libc++/libcompression/libarchive.2/libintl.8 (kit),
   libicucore (CoreFoundation). All the rest of /usr/lib is
   restore-only (libnetwork alone = 9.4 MB, libARI 3.7 MB).
4. **Minimal mount_filesystems** (without seputil/SEP, removed):
   `#!/bin/bash` + `/sbin/mount_apfs /dev/disk0s1s1 /mnt1` —
   the 14.6 system volume is disk0s1s1, Data is disk0s1s2.
5. **hdiutil create -srcfolder runs on the volume STILL MOUNTED**
   (sshrd.sh:313 does the same, detaching afterwards); UDRW (not UDZO) because
   the IM4P must wrap the raw bytes of the volume; fallback without
   -copyuid root if the privilege is missing (the ramdisk runs entirely as
   root, ownership does not matter).
6. Editing bugs fixed in passing: IM4P wrap pointed at
   rd.dmg (stock) instead of rd_small.dmg (slim) — exactly the
   bug class of point 6 in E32; duplicated comment block
   during the edit; invalid nested $(( )) arithmetic.

---

[← Previous](stage4-winning.md) · [Index](README.md) · [Next →](ssh-ramdisk.md)
