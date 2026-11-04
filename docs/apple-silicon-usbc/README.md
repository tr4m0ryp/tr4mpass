# Apple Silicon USB-C checkm8 research

> Part of the [tr4mpass project](../../README.md).

**TL;DR** — checkm8 works on an M-series Mac over a direct USB-C → Lightning cable once the host stops using libusb's cancel path: run all stages through IOKit, reset the handle between stages, and use the gaster stage-4 order. This folder is the evidence: constraints, the winning sequence, the pitfalls, and the E0–E68 lab log.

> **Scope note:** device names in the journal identify the specific test unit used for the evidence. The scripts do not hardcode them: `detect_ios.sh` reads the connected device's ProductType/board, and `fetch_images.sh` resolves every IPSW entry from `BuildManifest.plist`.

## Read this first

| Document | What it answers |
|---|---|
| [Why gaster cannot be copied verbatim](why-gaster-fails.md) | Why the Intel/gaster assumptions break on Apple Silicon XHCI |
| [XHCI constraints](xhci-constraints.md) | EP0 stalls, multi-packet DATA stages, timeouts, IOReturn codes |
| [Stage 4 sequence](stage4-sequence.md) | The exact overwrite/payload/reset order and the diagnostic traps |
| [Open leads](open-leads.md) | What is still open, and the current repository state |
| [Commands](commands.md) | Build, run, inspect the DFU serial |
| [References](references.md) | Upstream projects, articles and documentary sources |

## Experiment journal (E0 → E68)

| Part | Covers |
|---|---|
| [Experiment log — early runs (E0–E9)](journal/early-runs.md) | E0–E9, stage-4 summary, E20b and the validated M-series rules |
| [Stage 4 breakthrough (E10–E19)](journal/stage4-breakthrough.md) | E10–E17: libusb path, overwrite shape, reference gaster, structural diff |
| [Winning sequence (E17-first-thoughts–E19)](journal/stage4-winning.md) | E17-first-thoughts, E18, E19: the winning sequence and the PWND result |
| [Device model, pongoOS and KPF (E20–E33)](journal/pongoos-and-kpf.md) | E20–E33: real device model, exec channel, pongoOS and KPF boot |
| [SSH ramdisk, KPF wedge and boot matrix (E34–E68)](journal/ssh-ramdisk.md) | E34–E68: SSH ramdisk, KPF wedge, boot matrix and forensics |

The E-numbers are stable identifiers used by the code comments and the other documents. Start with [the journal index](journal/README.md) if you are looking for a specific experiment.
