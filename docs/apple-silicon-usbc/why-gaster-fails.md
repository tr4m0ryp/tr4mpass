# Why gaster cannot be copied verbatim

palera1n (which bundles gaster) explicitly documents that this configuration
is **not** reliably supported:

> "USB-C port on Apple Silicon Macs may require manual unplugging and replugging
> of the lightning cable **after checkm8 exploit**. This problem may be solved by
> connecting via USB hub."
>
> "USB-A cables are recommended to use, USB-C to may have issues with palera1n
> and getting into DFU mode."

In other words: the hub / the replug are **infrastructure workarounds**
used by the community, not a guarantee that the gaster flow works as
is. gaster's `checkm8_stage_patch()` assumes an EP0 that behaves as it does
on an Intel controller; on the XHCI of an M-series in direct USB-C, several of
its assumptions break down (see §3).

**Consequence**: all the macOS code of `checkm8_stage_patch()` in `tr4mpass`
is a **home-grown workaround attempt**, not a port of gaster. The code
comments must say so clearly (cf. §8, repository state).

Elements of gaster kept as a *protocol reference* (and not a transport one):

| Element | gaster | Notes |
|---|---|---|
| Order | overwrite → STALL → payload → 16 B suffix → zero-len DNLOAD → 3× GETSTATUS → ResetDevice + ReEnumerate | order preserved |
| A9 payload size | 528 B (176 main + 104 tail + 248 handler) | identical on our side |
| Overwrite | 48 B, `callback = insecure_memory_base` | identical |
| `usb_timeout` | **5 ms** (env `USB_TIMEOUT`, default 5) | see §3.4 |
| Payload | **a single** 528 B DNLOAD, **result ignored** | see §3.2 |
| CLR_STATUS of the spray | `wLength = 3*64+1 = 193` **with a buffer of zeros** | see §3.3 |
| Stage end | `ResetDevice` + `USBDeviceReEnumerate` | identical |

---

---

## Current architecture (recap)

```
stage 1  DFU reset        -> libusb
stage 2  setup (UAF)      -> libusb, asynchronous cancel mandatory
         ── switch ──> libusb close + IOKit activation (DeviceRequestTO)
stage 3  spray            -> IOKit
stage 4  overwrite + payload -> IOKit
```

The libusb → IOKit switch after stage 2 is **mandatory**: the asynchronous
cancellation of stage 2 triggers `AbortPipeZero` in the darwin backend of libusb,
which leaves libusb's async state machine unusable for the rest.
`DeviceRequestTO` (IOKit) is synchronous and works around this problem.

Useful details:

- `EP0_MAX_PACKET_SZ = 0x40` (64), `DFU_MAX_TRANSFER_SZ = 0x800` (2048).
- A9: `insecure_memory_base = 0x180380000`, `config_overwrite_pad = 0x500`.
- The marker expected in the serial is `PWND:`.

---

---

[Index](README.md) · [Next →](xhci-constraints.md)
