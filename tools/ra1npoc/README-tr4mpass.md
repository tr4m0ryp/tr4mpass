# ra1npoc (vendored)

Upstream: https://github.com/kok3shidoll/ra1npoc (branch `main`,
commit 3ee6d55, vendored 2026-11-01). MIT license (see LICENSE).

Open-source checkra1n-clone pongoOS loader. Payloads in `dumpfiles/`
are the dumped checkra1n v0.12.4 s8000 stage1/stage2 binaries plus the
matching pongoOS blob (serial `pongoOS / checkra1n 2.5.1`).

## tr4mpass patches (both in src/main.c, marked `tr4mpass E27/E28`)

1. **s8000 case**: upload pongoOS ONLY (`pongoOS_len = 0x3F2F8`, the
   exact size of `dumpfiles/pongoOS.bin`) and set `NO_AUTOBOOT`.
   Upstream s8000 uploads the full 1.4 MB blob (pongoOS+KPF+ramdisk),
   whose stage2 autoboot block reboots into iOS after ~seconds. With
   the truncated blob the pongoOS **shell stays alive indefinitely**
   (E28: verified interactive via pongoterm, `xargs`/`lsdev` OK).
2. **kpf_flags default**: `checkrain_option_pongo_shell` (unused with
   the truncated blob, kept for a future full-blob run).

## Build (macOS host)

```sh
tools/build_ra1npoc.sh        # produces build/ra1npoc + build/pongoterm
```

Upstream build steps preserved: `builtin.sh` regenerates
`src/payload/*.h` from `dumpfiles/` via xxd; `pongoOS/scripts`
builds `pongoterm` (the pongo shell client, IOKit + PID 0x4141).

## Role in the tr4mpass pipeline

Proven execution environment for step [4/8] on Apple Silicon USB-C
(docs/apple-silicon-usbc/journal/pongoos-and-kpf.md, E24–E28): pwn → stage2 → pongoOS
shell, then drive ramdisk/KPF/bootx through the shell. Supersedes the
iBSS/iBEC transition route for this machine.
