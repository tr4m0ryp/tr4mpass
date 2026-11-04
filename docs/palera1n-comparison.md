# tr4mpass vs palera1n — two routes, two objectives

> Part of the [documentation index](README.md).

> **Note**: this document compares the **technical routes** of two tools that
> share the same entry point (checkm8) but pursue different objectives. It was
> written after the validation of the checkm8 stage on Apple Silicon Macs over
> direct USB-C → Lightning (see
> [`apple-silicon-usbc/`](apple-silicon-usbc/README.md),
> experiment E19).

---

## 1. The objective, stated unambiguously

An iPhone stuck on the **Setup Assistant** is unusable: no access to the home
screen, no network, no usable SIM. Unlocking it requires **four** distinct
things:

| # | What must be achieved | Why it is necessary |
|---|---|---|
| 1 | The BootROM accepts unsigned code | Without it, no third-party code runs on the device |
| 2 | A temporary execution environment (ramdisk) | Lets you write to the file system |
| 3 | Activation recorded on the `mobileactivationd` side | Otherwise iOS stays on the Setup screen |
| 4 | Setup Assistant neutralized (`Setup.app` + PurpleBuddy) | Otherwise it restarts at boot |

**"Signal preserved"** means step 3 is done *cleanly*: the baseband stays
paired, the IMEI/MEID is recognized, calls and data work. A botched activation
gives you a device that boots but stays on "Searching…" with no cellular
network.

That is what tr4mpass aims for. **palera1n does not aim for that** — it aims
for a *jailbreak* (running unsigned code permanently, installing packages).
Both need steps 1 and 2, then they diverge completely.

---

## 2. The tr4mpass route — Path A (A5 → A11)

```mermaid
flowchart TD
    A["iPhone stuck<br/>Setup Assistant"] --> B["DFU mode<br/>05ac:1227"]
    B --> C["checkm8<br/>4 stages: reset / setup / spray / patch"]
    C --> D["BootROM accepts unsigned<br/>serial = PWND:[checkm8]"]
    D --> E["Upload iBSS<br/>DFU → recovery transition 0x1281"]
    E --> F["Upload iBEC + go<br/>recovery → iBEC transition"]
    F --> G["Upload devicetree + ramdisk<br/>+ bootx"]
    G --> H["Ramdisk booted<br/>SSH on 127.0.0.1:2222"]
    H --> I["Kernel patches<br/>rootfs rw + AMFI / dyld"]
    I --> J["Replacing<br/>mobileactivationd"]
    J --> K["Building + sending<br/>the activation record"]
    K --> L["deletescript<br/>Setup.app → .bak<br/>PurpleBuddy SetupDone<br/>purge records + caches"]
    L --> M["Verification<br/>activation_is_activated"]
    M --> N["DEVICE ACTIVATED<br/>Cellular signal preserved"]

    style C fill:#2d5016,color:#fff
    style N fill:#1a5c2e,color:#fff
    style J fill:#5c4a1a,color:#fff
    style K fill:#5c4a1a,color:#fff
    style L fill:#5c4a1a,color:#fff
```

**USB transitions: 3** (DFU → recovery → iBEC → bootx). Each one is a potential
point of fragility on Apple Silicon USB-C ports — this is exactly what palera1n
documents as problematic downstream of the pwn.

**What produces persistence**: the files written to the file system
(`mobileactivationd` replaced, `Setup.app` renamed, plists). The device stays
activated after reboot, because the state is **on disk**.

---

## 3. The palera1n route (v3.0.0-beta.3)

```mermaid
flowchart TD
    A2["iPhone (ideally activated)"] --> B2["DFU mode<br/>05ac:1227"]
    B2 --> C2["checkm8<br/>via libopenra1n (MIT)"]
    C2 --> D2["lz4dec.bin + compressed PongoOS<br/>A SINGLE transition → 0x4141"]
    D2 --> E2["PongoOS shell"]
    E2 --> F2["fuse lock<br/>sep auto"]
    F2 --> G2["Upload KPF<br/>modload<br/>(AMFI / sandbox patches in RAM)"]
    G2 --> H2["Upload jbinit ramdisk<br/>+ binpack.tar → overlay"]
    H2 --> I2["xargs boot_args<br/>+ bootx"]
    I2 --> J2["Real iOS kernel<br/>patched IN RAM"]
    J2 --> K2["palera1nLoader<br/>installs the bootstrap (Sileo)"]
    K2 --> L2["JAILBROKEN<br/>but still on<br/>Setup Assistant if not activated"]

    style C2 fill:#2d5016,color:#fff
    style D2 fill:#1a3d5c,color:#fff
    style L2 fill:#5c1a1a,color:#fff
```

**USB transitions: 1** (DFU → PongoOS). That is its real technical advantage on
Apple Silicon: a single switchover, the same mechanism that already works.

**What produces "persistence"**: two layers.
- The kernel patches live **in RAM only** → lost on every reboot (hence the
  *semi-tethered* nature).
- The bootstrap (Sileo, tweaks) lives on a 2nd APFS volume ("fakefs") or
  `/var/jb` → persists.

**What does not persist**: activation. palera1n never touches it.

---

## 4. Where the two routes separate

```mermaid
flowchart LR
    DFU["DFU mode"] --> PWN["checkm8<br/>PWND obtained"]
    PWN --> RAMDISK["Temporary environment<br/>(ramdisk / PongoOS)"]
    RAMDISK --> FORK{"Objective?"}

    FORK -->|"tr4mpass"| ACT["Activation"]
    FORK -->|"palera1n"| JB["Jailbreak"]

    ACT --> ACT1["mobileactivationd<br/>replaced"]
    ACT1 --> ACT2["Activation record<br/>submitted"]
    ACT2 --> ACT3["Setup.app neutralized"]
    ACT3 --> ACT4["State written TO DISK<br/>→ persists across reboot"]

    JB --> JB1["KPF: kernel patches"]
    JB1 --> JB2["Bootstrap installed<br/>(Sileo, packages)"]
    JB2 --> JB3["Patches IN RAM<br/>→ lost on reboot<br/>semi-tethered"]

    style ACT fill:#1a5c2e,color:#fff
    style JB1 fill:#1a3d5c,color:#fff
    style JB3 fill:#5c1a1a,color:#fff
```

Both consume the same steps 1 and 2. **Everything that follows diverges.**
An unactivated device remains unactivated after palera1n, whatever the success
of its jailbreak.

---

## 5. Comparison table — what one does, what the other does not

### 5.1 Capabilities

| Capability | tr4mpass (Path A) | palera1n |
|---|:---:|:---:|
| Exploit checkm8 (A5 → A11) | ✅ | ✅ |
| Obtain `PWND:[checkm8]` | ✅ | ✅ |
| Boot an unsigned environment | ✅ iOS ramdisk | ✅ PongoOS |
| Patch the kernel | ✅ (rootfs rw + AMFI/dyld) | ✅ (KPF: AMFI, sandbox) |
| **Submit an activation record** | ✅ `mobileactivation_activate` | ❌ **never** |
| **Replace `mobileactivationd`** | ✅ backup + SCP + chmod/chown + verification | ❌ **never** |
| **Neutralize `Setup.app`** | ✅ renamed to `.bak` (reversible) | ❌ **never** |
| **Mark PurpleBuddy `SetupDone`** | ✅ plist write | ❌ **never** |
| **Purge activation records** | ✅ | ❌ **never** |
| **Verify the final activation state** | ✅ `activation_is_activated` | ❌ **never** |
| **"Signal preserved" logic** | ✅ GSM / CDMA / dual detection | ❌ **never** |
| Install a package manager | ❌ | ✅ Sileo via bootstrap |
| Install tweaks / dylibs | ❌ | ✅ |
| Support A12+ (Path B) | ✅ | ❌ (checkm8 stops at A11) |
| Work on an **already activated** device | ✅ | ✅ |
| Work on a **non-activated** device | ✅ (that is the goal) | ⚠️ jailbreak yes, activation no |

### 5.2 Transport and reliability (Apple Silicon Mac, direct USB-C)

| Criterion | tr4mpass (Path A) | palera1n |
|---|---|---|
| Promoted USB backend | Direct IOKit (`DeviceRequestTO`) | IOKit (libopenra1n) |
| libusb backend | ✅ (Linux/Windows) | ✅ (alternative build) |
| **USB transitions after the pwn** | **3** | **1** |
| Exposure to the M-series USB-C bug | high (documented area) | low |
| Reboot required after the pwn | no | no |
| State after reboot | ⚠️ depends on the disk write | semi-tethered (restart the tool) |
| Requires an IPSW | ✅ yes (iBSS/iBEC/devicetree/ramdisk) | ❌ no (embedded payloads) |
| Requires pongoOS payloads | ❌ no | ✅ yes (CI artifacts) |
| License | — | MIT (libopenra1n) |

### 5.3 Persistence layers

| Layer | tr4mpass | palera1n |
|---|---|---|
| Kernel patches | applied from the ramdisk, **written to disk** | **RAM only** |
| `mobileactivationd` | ✅ replaced on `/mnt1/usr/libexec/` | — |
| Bootstrap / packages | — | 2nd APFS volume or `/var/jb` |
| User-visible apps | — | palera1nLoader in `/Applications` |
| Survives reboot | ✅ yes (disk state) | ❌ no for the kernel |

### 5.4 Validation status (honesty)

| Element | tr4mpass | palera1n |
|---|---|---|
| checkm8 on this machine | ✅ **proven E19** (`PWND:[checkm8]`) | ✅ proven E16 |
| Steps 3 through 8 (activation) | ⚠️ **written, never executed** | — (do not exist) |
| Full jailbreak | — | ✅ tool in production |
| Tests | 62/62 unit | — |

> **This is the most important point of this document.** tr4mpass's activation
> code (`src/bypass/`, `src/activation/`, ~2980 lines) is complete but has
> **never run on a device**, because checkm8 was blocking everything upstream.
> The E19 validation lifts that blockage for the first time. palera1n,
> conversely, is battle-tested — but does not know how to activate.

---

## 6. What there really is to borrow from palera1n

A single element, and it is not business logic code:

**The single-transition transport.** The idea of having the stage 4 payload
carried by a complete environment (compressed PongoOS + `lz4dec.bin`) rather
than by a small shellcode, so that the device switches directly into an
exploitable mode without going back through recovery.

```mermaid
flowchart LR
    subgraph TR["tr4mpass — IPSW route"]
        direction TB
        T1["pwn"] --> T2["recovery<br/>0x1281"]
        T2 --> T3["iBEC"]
        T3 --> T4["recovery<br/>iBEC context"]
        T4 --> T5["ramdisk"]
    end
    subgraph PA["palera1n — PongoOS route"]
        direction TB
        P1["pwn"] --> P2["PongoOS<br/>0x4141"]
        P2 --> P3["everything else<br/>via the shell"]
    end

    style T2 fill:#5c1a1a,color:#fff
    style T3 fill:#5c1a1a,color:#fff
    style T4 fill:#5c1a1a,color:#fff
    style P2 fill:#1a5c2e,color:#fff
```

The three red steps are three chances to lose the device on an Apple Silicon
USB-C port. The green step is a single chance.

**But**: adopting this route means giving up the iOS ramdisk extracted from an
IPSW, and therefore finding an alternative ramdisk containing the activation
tools (`mobileactivationd`, AFC, SSH…) — that is a project, not a port.

**Decision**: only do it if the iBSS transition test fails.
If the 3 transitions pass, the IPSW route (already fully written) remains the
shortest.

---

## 7. Summary in one sentence

> **palera1n** unlocks the inside of the system but leaves the front door
> closed; **tr4mpass** opens the front door, and still has to prove that it
> can do so end to end.

---

## See also

- [`apple-silicon-usbc/`](apple-silicon-usbc/README.md) — complete E0 → E20b log
- [`README.md`](README.md) — tr4mpass user documentation
- [palera1n](https://github.com/palera1n/palera1n) — upstream repository (`v3.0.0-beta.3` branch analyzed)
