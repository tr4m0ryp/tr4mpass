# Experiment log — Apple Silicon USB-C checkm8

**TL;DR** — chronological lab log from the first failed gaster attempts to the working pongoOS/KPF/SSH-ramdisk route. Each part is self-contained; start with the [research index](../README.md) if you are new.

| Part | Covers |
|---|---|
| [Experiment log — early runs (E0–E9)](early-runs.md) | E0–E9, stage-4 summary, E20b and the validated M-series rules |
| [Stage 4 breakthrough (E10–E19)](stage4-breakthrough.md) | E10–E17: libusb path, overwrite shape, reference gaster, structural diff |
| [Winning sequence (E17-first-thoughts–E19)](stage4-winning.md) | E17-first-thoughts, E18, E19: the winning sequence and the PWND result |
| [Device model, pongoOS and KPF (E20–E33)](pongoos-and-kpf.md) | E20–E33: real device model, exec channel, pongoOS and KPF boot |
| [SSH ramdisk, KPF wedge and boot matrix (E34–E68)](ssh-ramdisk.md) | E34–E68: SSH ramdisk, KPF wedge, boot matrix and forensics |

The E-numbers are stable identifiers used by the code comments and the other documents. Search for `E19`, `E32`, `E64`, etc. to jump to a specific experiment.
