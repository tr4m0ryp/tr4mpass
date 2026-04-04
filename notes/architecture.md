# Itr4m -- Architecture Design
# Created: 2026-04-05

## Design Principles

1. Extend the existing C framework from research_icloud0/src/ -- do not rewrite
2. Max 300 lines per source file (split proactively at ~200)
3. Plugin architecture for bypass modules (already in bypass.h)
4. Layered: USB/DFU at bottom, device abstraction, bypass strategies on top
5. Single binary (itr4m) + start.sh wrapper for guided flow
6. All deps via pkg-config; Makefile auto-discovers src/**/*.c

## Existing Code to Carry Forward

From research_icloud0/src/:
- device.h/c    -- device_info_t, detect/query/print/free (libimobiledevice)
- bypass.h/c    -- bypass_module_t plugin interface, register/select/execute
- afc_utils.h/c -- AFC file operations wrapper
- activation.h/c -- mobileactivation service client (state/session/info/submit)

These are well-designed and match our needs. We extend, not replace.

## Directory Layout

```
Itr4m/
|-- start.sh                  # Guided entry point (DFU instructions, device check)
|-- Makefile                   # Auto-discovers src/**/*.c, pkg-config deps
|-- include/
|   |-- itr4m.h               # Top-level: version, common types, error codes
|   |-- device/
|   |   |-- device.h          # device_info_t (extended from existing)
|   |   |-- usb_dfu.h         # DFU mode USB comms (libusb)
|   |   `-- chip_db.h         # CPID lookup table, chip_info_t
|   |-- exploit/
|   |   |-- exploit.h         # exploit_ctx_t, exploit_deliver()
|   |   |-- checkm8.h         # checkm8-specific: stages, payloads
|   |   `-- dfu_proto.h       # DFU protocol constants + state machine
|   |-- bypass/
|   |   |-- bypass.h          # bypass_module_t plugin interface (existing)
|   |   |-- path_a.h          # A5-A11: checkm8 + checkra1n + NVRAM
|   |   |-- path_b.h          # A12+: identity manip + session activation
|   |   `-- deletescript.h    # iOS-version-specific cleanup
|   |-- activation/
|   |   |-- activation.h      # mobileactivation client (existing, extended)
|   |   |-- session.h         # drmHandshake session mode protocol
|   |   `-- record.h          # Activation record builder (plist construction)
|   `-- util/
|       |-- log.h             # Logging (levels, color terminal output)
|       |-- plist_helpers.h   # Plist construction/parsing helpers
|       `-- usb_helpers.h     # Shared USB utility functions
|
|-- src/
|   |-- main.c                # Entry point, CLI arg parsing, orchestration
|   |-- device/
|   |   |-- device.c          # Device detection + info query (libimobiledevice)
|   |   |-- usb_dfu.c         # DFU device detection + USB I/O (libusb)
|   |   `-- chip_db.c         # CPID->chip_info lookup, offset tables
|   |-- exploit/
|   |   |-- dfu_proto.c       # DFU state machine (DNLOAD, UPLOAD, GETSTATUS...)
|   |   |-- checkm8.c         # checkm8 exploit: reset, spray, setup, patch
|   |   `-- payload/          # Pre-built binary payloads
|   |       |-- rop_a10.h     # A10 ROP chain + offsets
|   |       |-- rop_a11.h     # A11 ROP chain + offsets
|   |       |-- shellcode.h   # Stage1 shellcode (embedded as byte arrays)
|   |       `-- ramdisk.h     # Ramdisk loading helpers
|   |-- bypass/
|   |   |-- bypass.c          # Module registry (existing)
|   |   |-- path_a.c          # A5-A11 bypass: checkm8 -> jailbreak -> NVRAM
|   |   |-- path_a_jailbreak.c # Ramdisk load, kernel patches, SSH setup
|   |   |-- path_b.c          # A12+ bypass: identity manip -> session activate
|   |   |-- path_b_identity.c # Serial/ECID descriptor manipulation in DFU
|   |   |-- deletescript.c    # Setup.app removal, purgebuddy, activation purge
|   |   `-- signal.c          # Signal vs no-signal detection + baseband handling
|   |-- activation/
|   |   |-- activation.c      # mobileactivation client (existing, extended)
|   |   |-- session.c         # drmHandshake: session info -> handshake -> activate
|   |   `-- record.c          # Build activation record plists from device info
|   `-- util/
|       |-- log.c             # Logging implementation
|       |-- plist_helpers.c   # Plist construction utilities
|       `-- usb_helpers.c     # Shared USB routines
|
|-- payloads/                  # Binary blobs (not compiled, just included)
|   |-- boot.tar.lzma         # Ramdisk archive (extracted from IPSW + patched)
|   `-- boot.raw              # Raw ramdisk image
|
|-- scripts/
|   `-- build_ramdisk.sh      # Extract + patch ramdisk from IPSW
|
`-- notes/                     # Design docs (already exists)
```

## Key Data Structures

### device_info_t (extended)

```c
typedef struct {
    /* Identification */
    char udid[64];
    char product_type[128];     /* "iPad8,10" */
    char ios_version[128];      /* "26.1" */
    char serial[128];
    char imei[128];
    char meid[128];             /* NEW: for MEID bypass path */
    char hardware_model[128];
    char device_name[128];
    char activation_state[128];

    /* Chip identification */
    uint32_t cpid;              /* NEW: numeric chip ID (0x8027 = A12Z) */
    uint64_t ecid;              /* NEW: exclusive chip ID */
    char chip_name[32];         /* NEW: "A10", "A11", "A12", etc. */
    int checkm8_vulnerable;     /* NEW: 1 if A5-A11, 0 if A12+ */

    /* Connectivity */
    int is_cellular;            /* NEW: 1 if cellular, 0 if WiFi-only */
    int is_dfu_mode;            /* NEW: 1 if in DFU mode */

    /* Handles */
    idevice_t handle;           /* libimobiledevice */
    lockdownd_client_t lockdown;
    libusb_device_handle *usb;  /* NEW: for DFU mode (NULL if not DFU) */
} device_info_t;
```

### chip_info_t

```c
typedef struct {
    uint32_t cpid;
    const char *name;           /* "A10", "A11", "A12", etc. */
    int checkm8_vulnerable;     /* 1 for A5-A11, 0 for A12+ */
    const uint8_t *rop_chain;   /* NULL for A12+ */
    size_t rop_len;
    uint32_t heap_base;         /* DFU heap base address */
    uint32_t payload_base;      /* Shellcode load address */
} chip_info_t;
```

### exploit_ctx_t

```c
typedef struct {
    device_info_t *dev;
    const chip_info_t *chip;
    int phase;                  /* 0=init, 1=reset, 2=spray, 3=setup, 4=patch */
    uint8_t *payload_buf;
    size_t payload_len;
} exploit_ctx_t;
```

### bypass_module_t (unchanged -- existing interface works)

```c
typedef struct bypass_module {
    char name[64];
    char description[256];
    int (*probe)(device_info_t *dev);
    int (*execute)(device_info_t *dev);
} bypass_module_t;
```

## Control Flow

### main.c orchestration

```
main()
  |
  +-- parse_args(argc, argv)        # --verbose, --dry-run, --force-path-a/b
  |
  +-- device_detect_any(&dev)       # Try DFU (libusb) first, then normal (usbmuxd)
  |     |
  |     +-- usb_dfu_find()          # VID=0x05AC PID=0x1227 -> DFU mode
  |     +-- device_detect()         # Fallback: libimobiledevice usbmuxd
  |
  +-- device_query_info(&dev)       # Populate all fields
  |     |
  |     +-- chip_db_lookup(cpid)    # Map CPID -> chip name + vulnerability flag
  |     +-- detect_cellular()       # Check for baseband presence
  |
  +-- device_print_info(&dev)       # Show user what we found
  |
  +-- register_modules()            # Register path_a and path_b modules
  |     |
  |     +-- bypass_register(&path_a_module)
  |     +-- bypass_register(&path_b_module)
  |
  +-- bypass_select(&dev)           # path_a.probe() checks checkm8_vulnerable
  |                                 # path_b.probe() checks A12+
  |
  +-- bypass_execute(mod, &dev)     # Run selected path
  |
  +-- device_free(&dev)
```

### Path A flow (A5-A11)

```
path_a_execute(dev)
  |
  +-- checkm8_exploit(dev)          # DFU exploit delivery
  |     +-- checkm8_stage_reset()
  |     +-- checkm8_stage_spray()
  |     +-- checkm8_stage_setup()
  |     +-- checkm8_stage_patch()   # Shellcode loaded, serial shows PWND:
  |
  +-- ramdisk_load(dev)             # Send boot.tar.lzma via pwned DFU
  |
  +-- jailbreak_apply(dev)          # Kernel patches via ramdisk
  |     +-- nvram_unlock()
  |     +-- mac_mount()
  |     +-- dyld_patch()
  |
  +-- activation_bypass(dev)        # Local activation
  |     +-- replace_mobileactivationd()
  |     +-- record_build(dev, &record)
  |     +-- activation_submit_record(dev, record)
  |
  +-- deletescript_run(dev)         # iOS-version-specific cleanup
  |     +-- remove_setup_app()
  |     +-- set_purplebuddy_done()
  |     +-- purge_activation_records()
  |
  +-- verify_bypass(dev)            # Check ActivationState == Acknowledged
```

### Path B flow (A12+)

```
path_b_execute(dev)
  |
  +-- identity_manipulate(dev)      # Modify serial descriptors in DFU
  |     +-- usb_dfu_write_serial()  # Append PWND:[checkm8] marker
  |
  +-- signal_detect(dev)            # Cellular vs WiFi-only
  |     +-- is_cellular(dev)
  |     +-- select_signal_path()    # Signal or no-signal variant
  |
  +-- session_activate(dev)         # drmHandshake session mode
  |     +-- session_get_info(dev)               # CreateTunnel1SessionInfoRequest
  |     +-- session_drm_handshake(dev, &resp)   # POST drmHandshake (local)
  |     +-- session_create_activation(dev, resp) # CreateTunnel1ActivationInfoRequest
  |     +-- record_build_a12(dev, &record)       # Build FActivation record
  |     +-- session_activate_final(dev, record)  # HandleActivationInfoWithSessionRequest
  |
  +-- deletescript_run(dev)         # Same as Path A
  |
  +-- verify_bypass(dev)            # Check ActivationState
```

## start.sh Flow

```bash
#!/bin/bash
# Itr4m -- Guided activation lock bypass

echo "=== Itr4m v0.1 ==="
echo "Connect your iOS device via USB."
echo ""

# Check dependencies
check_deps() {
    for cmd in itr4m libusb-config; do
        command -v "$cmd" >/dev/null || { echo "Missing: $cmd"; exit 1; }
    done
}

# Check if device is in DFU mode
check_dfu() {
    if system_profiler SPUSBDataType 2>/dev/null | grep -q "0x1227"; then
        echo "[+] Device detected in DFU mode"
        return 0
    fi
    return 1
}

# Guide user into DFU mode
guide_dfu() {
    echo "Put your device into DFU mode:"
    echo "  1. Connect device to Mac via USB"
    echo "  2. Power off the device"
    echo "  3. Hold Power + Home (or Volume Down) for 10 seconds"
    echo "  4. Release Power, keep holding Home (or Volume Down) for 5 more seconds"
    echo "  5. Screen should be BLACK (not Apple logo)"
    echo ""
    echo "Press Enter when ready..."
    read -r
}

check_deps

# Try normal mode first
if ./itr4m --detect-only 2>/dev/null; then
    echo "[+] Device detected in normal mode"
else
    # Guide to DFU
    guide_dfu
    if ! check_dfu; then
        echo "[-] Device not detected in DFU mode. Try again."
        exit 1
    fi
fi

# Run the bypass
./itr4m "$@"
```

## Build System

```makefile
CC       = cc
CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2
LDFLAGS  =

# Library discovery via pkg-config
PKG_LIBS = libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 \
           libplist-2.0 openssl

CFLAGS  += $(shell pkg-config --cflags $(PKG_LIBS) 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs $(PKG_LIBS) 2>/dev/null)

# Auto-discover sources
SRCS     = $(shell find src -name '*.c')
OBJS     = $(SRCS:.c=.o)
TARGET   = itr4m

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	find src -name '*.o' -delete
	rm -f $(TARGET)

.PHONY: all clean
```

## Dependencies

Required (install via Homebrew):
- libimobiledevice  (device communication, lockdownd, mobileactivation)
- libirecovery      (DFU/recovery mode communication)
- libusb            (raw USB for DFU exploit delivery)
- libplist          (plist construction and parsing)
- openssl           (crypto: AES, RSA, SHA for activation records)

## Module Responsibilities (max 300 lines each)

| File                   | Responsibility                                    | Deps              |
|------------------------|---------------------------------------------------|-------------------|
| main.c                 | CLI parsing, orchestration, module registration   | all               |
| device/device.c        | libimobiledevice detection + query                | libimobiledevice  |
| device/usb_dfu.c       | libusb DFU device detection + raw USB I/O         | libusb            |
| device/chip_db.c       | CPID -> chip_info_t lookup table                  | none              |
| exploit/dfu_proto.c    | DFU protocol: DNLOAD, UPLOAD, GETSTATUS, ABORT    | libusb            |
| exploit/checkm8.c      | checkm8: heap feng-shui, UAF, ROP, shellcode     | dfu_proto, chip_db|
| bypass/bypass.c        | Module registry, select, execute                  | device            |
| bypass/path_a.c        | A5-A11 orchestration: exploit -> jailbreak -> act | exploit, activation|
| bypass/path_a_jailbreak.c | Ramdisk load, kernel patches, SSH               | usb_dfu, afc      |
| bypass/path_b.c        | A12+ orchestration: identity -> session -> act    | activation, session|
| bypass/path_b_identity.c | DFU serial descriptor manipulation               | usb_dfu           |
| bypass/deletescript.c  | iOS-version cleanup: Setup.app, purgebuddy, etc   | afc               |
| bypass/signal.c        | Cellular/WiFi detection, signal path selection    | device            |
| activation/activation.c | mobileactivation service client                  | libimobiledevice  |
| activation/session.c   | drmHandshake session protocol (two-stage)         | activation, plist |
| activation/record.c    | Build activation record plists from device info   | libplist, openssl |
| util/log.c             | Logging with levels and color                     | none              |
| util/plist_helpers.c   | Plist dict/array construction shortcuts            | libplist          |
| util/usb_helpers.c     | Shared USB control transfer wrappers              | libusb            |
