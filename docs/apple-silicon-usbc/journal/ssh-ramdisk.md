# SSH ramdisk, KPF wedge and boot matrix (E34–E68)

| ID | Type | Finding | Decision/impact |
|---|---|---|---|
| E34–E50 | ⚠️ | Bypass tooling period: study of iPhone-Hacktivation-Toolkit, README conofor read in full, `mobileactivationd_ios13` downloaded and sha256 verified (`2454be8c…`, identical to the toolkit blob `99ed5e11…`), placed on the device as an inert copy `mobileactivationd_ios13_p24` | no public patched binary for iOS 14.x exists — a real port must patch the stock 14.6 binary |
| E51–E53 | ✅ | "both renamed" state (`Setup.app` + `mobileactivationd` → `.tr4mpass`) freeboot **OK** that morning (screen: different greeter, not Setup.app) | observed once, not reproduced to date (cf. E67) |
| E55–E57 | ⚠️ | Loader chain stabilized; multiple pongo/KPF landings (logs `e55_*`, `pongo-load-*`) | prepares the E60 KPF debugging |
| E58 | ❌ | Pipeline rebuild lost `--kpf` → silent boot without KPF | `--kpf` is opt-in in `scripts/pongo_load_ramdisk.sh`: always check `cmd: modload` ≥ 1 in the pongo logs after each run |
| #24 | ❌ | iOS-13 binary patched at the canonical location `/usr/libexec/mobileactivationd` → Apple bootloop (~30 s then reboot) | dyld 14.6 rejects the 13 binary → eternal launchd respawn; the loop disappears when the volume returns to factory layout |

## E60–E63 — THE KPF WEDGE: COMPLETE ROOT CAUSE

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E60–E62** | ❌ | KPF upload: the 5 data chunks go through, then the **trailing zero-pad write** (184 B) stalls (`WritePipe at 0: 0xe0004051`) 3×/3; policy `36deb87` = full restart → deterministic re-stall | the full restart is fatal here; the old "recovering" build (in-place retry) had solved this same stall: indexed as the correct policy | edit `tools/pongo_upload.c` |
| **E63** | ❌ | On a fresh pongo, very first transaction: exactly the same stall → "upload order" and "50 MB first" hypotheses **falsified** | the wedge is the **shortened final write** itself[^note] ; the host-side ClearPipeStallBothEnds/r never unblocks it | host-side padding |
| **pre-E64** | ✅✅ | KPF pre-padded to 16 KiB (`kpf_checkra1n_pad.bin`, 98,304 B): upload OK `+0 pad`… but `modload: short read` → **key discovery**: pongoOS completion is signalled on a **short final OUT chunk**; aligning to 16 KiB removes the terminator | the right padding = a multiple of 1 KiB **NOT** 16 KiB: `kpf_checkra1n_1k.bin` (88,064 B, +440 zeros); the short final chunk completes the xfer and the zeros behind it are ignored (pongo sizes by `newsz`) | canonical artifact for all future uploads |
| **pre-E64** | ⚠️ | Editing bug caught by the log: with `pad=0`, the loop fell back into "pad retries exhausted" instead of success | added the early-return success path `pad==0` in `tools/pongo_upload.c` | — |

Summary of `tools/pongo_upload.c` (edited, not committed as of the date of this entry):
pad retry **in place** up to 8× (safe: pad = zeros + xfer sized by `newsz`, the surplus is ignored — duplication is therefore harmless *here*, UNLIKE a mid-file retry — cf. lesson E39/E41), + early success `pad==0`. Single build warning: unused static `ifc_reopen` (no impact).

## E64 — CANONICAL RECIPE + FALSIFICATION OF THE "PIN SNAPSHOT"

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E64** | ✅✅ | Full chain first try: DFU 0x1227 → ra1npoc → pongo 16705 → KPF-padded-first (`KPF_OK`) → `modload` → ramdisk 50,331,648 B (`RD_OK`) → `xargs rootdev=md0 rd=md0 -v wdt=-1 trm_default=0` → `xfb` → `bootx` → `SHELL_OK` TRY1 | deterministic recipe; **factory restore (canonical Setup.app + mobileactivationd)** → freeboot Setup screen ✅ → the freeboot reads the **LIVE volume**; the "pin snapshot" (xid 65) that framed the strategy becomes an obsolete dead end — `apfs_sealvolume` never needed (iOS 14.6, no SSV) | journal + commit |

Canonical boot recipe starting from E64:
```
DFU 0x1227 → ra1npoc → pongo 16705 →
upload kpf_checkra1n_1k.bin (88 064 B) → cmd modload →
upload ramdisk_ssh_raw.hfs (50 331 648 B) → cmd ramdisk →
cmd "xargs rootdev=md0 rd=md0 -v wdt=-1 trm_default=0" → xfb → bootx
→ iproxy 2223 22 → ssh_expect22 'echo SHELL_OK'
```

## E65–E67 — "ACTIVATION STACK" BOOT MATRIX (one variable per run)

| ID | Setup.app state | mobileactivationd state | Freeboot | Signature |
|---|---|---|---|---|
| E64 | present | present (canonical) | ✅ Setup screen | factory |
| morning E51–E53 | renamed | renamed | ✅ (other greeter) | observed once |
| #24 | present | iOS-13 binary | ❌ | logo ~30 s → reboot (launchd respawn, dyld reject) |
| **E65** | **present, intact** | **absent** (mv → `.tr4mpass`, cp `.orig_backup` BEFORE) | ❌ | Apple logo ~30 s → reboot, no kernel text visible |
| **E66** | **absent** (mv → `.tr4mpass`) | **present, original** (restored from `.orig_backup`) | ❌ | **different signature**: Apple logo → black screen ~1 s → Apple logo | 
| **E67** | absent | absent (mv → `.ren2`) | ❌ | loop — **contradicts E51–E53**: that morning, this same state had booted |

Honest reading of the table:
1. The activation stack supports "everything present" or "everything absent" — any intermediate state gets stuck, at different depths (E65 dies at the launchd respawn; E66 dies later, at the greeter pass).
2. **E67 is the real contradiction**: disk state identical to E51–E53 (both renamed) but the freeboot loops. ⇒ **the cause of the loop since E65 is no longer only the System renames**: something else has drifted (Data-volume state accumulated by the crash cycles, unclean mounts in series, or NVRAM/prefs) — the "file ledger" theory is insufficient on its own.

## E68 — COMPLETE FACTORY RECOVERY + FIRST FORENSICS PASS (BLOCKED ON Data)

| ID | Type | Finding | Decision/impact | Next action |
|---|---|---|---|---|
| **E68a** | ✅✅ | Full restore in a single DFU: `Setup.app.tr4mpass → Setup.app` + `mobileactivationd.ren2 → mobileactivationd`, `sync=0`, `umount=0`, reboot | disk state = factory (the only inert extras: `.tr4mpass` ×2, `.ios13_p24`, `.ren2`) | expected freeboot: Setup screen (as in E64) |
| **E68b** | ⚠️ | Container mapping via `fsck_apfs -ql /dev/disk0s1`: exact order **System, Data, xART, Baseband Data, Hardware, Preboot, Update**; cross-checked with the RO mounts of s4(bbfs)/s5(FactoryData)/s6(Preboot UUID+active)/s7(MobileAsset/restore.log) ⇒ **Data = `disk0s1s2`** | extra s2/s3 dev nodes appeared mid-session; s4–s7 RO mounts all OK (`{m4..m7}=0`) | Data mount first on the **next** DFU |

Forensics failure of the day, documented precisely:
- `mount_apfs -o ro /dev/disk0s1s2 /mnt6` (and rw, and form `Data`, and with
  System unmounted) ⇒ **"Resource busy" (rc=75)** in ALL forms.
- `fsck_apfs -ql /dev/disk0s1s2` **works** and prints "FILESYSTEM
  CLEAN" ⇒ the volume is readable and healthy; it is the *mount* alone that is
  refused at kernel level. `snaputil -l` on s2 ⇒ "Invalid argument"
  (same class of refusal by the smart ioctls).
- `disk1` is not APFS (raw/encrypted — out of scope).
- Leading theory: the **first** combined command of the day (the one that
  "vanished" without output after the password on the s2 mount) left the
  volume attached-but-not-mounted; the kernel refuses any subsequent mount
  of an already-attached volume. Discriminating test: fresh sshrd session,
  Data mounted **first** before any other mount.
- If the refusal persists in a fresh session ⇒ policy refusal by the kernel
  (volume key unwrapping in the sshrd context) ⇒ Data forensics via
  another tool (fsck-level extraction, or a snappy-style helper for 14.6).

## WHERE WE STAND AT THE CLOSE OF THIS SESSION (2026-11-02)

- Device: **full factory** disk state, clean checkpoint, reboot
  sent — expected freeboot: Setup screen.
- Canonical artifacts: `~/.cache/tr4mpass/images/kpf_checkra1n_1k.bin`
  (88,064 B — the only correct padding); `kpf_checkra1n_pad.bin` (98,304 B)
  kept only as a negative artifact (16 KiB = terminator killed).
- Tools: `tools/pongo_upload.c` (in-place pad-retry + early `pad==0`) and
  `scripts/build_ssh_rd.sh` (`TR4MPASS_SKIP_AFC=1`) **edited, not
  committed as of the time of this writing** — commit with this entry.
- Reference logs: `~/.cache/tr4mpass/logs/e64_* … e68_*` +
  `pongo-load-*`.
- **Open, to be handled in the next session**:
  1. Data-volume forensics in a fresh sshrd session (Data mount FIRST) —
     target: `/var/db/diagnostics/Persist/*.tracev3`,
     `/var/mobile/Library/Logs/CrashReporter/*.ips`,
     `/var/db/com.apple.xpc.launchd/…` → answers on the actual mechanism
     of the E65/E66/E67 loops.
  2. remaining run ladder: ldid-signed exit-0 stub vs real patched 14.6
     daemon (conofor port) — E66/E67 overturn the previous hypotheses;
     *no* "half-renamed" config is bootable, only the pair
     (present+present) or (absent+absent) has been.
  3. full test suite (62) once, orchestrated, as agreed.

---

[← Previous](pongoos-and-kpf.md) · [Index](README.md)
