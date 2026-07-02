# Libirecovery API Compatibility

## Overview

tr4mpass supports both **legacy** and **modern** versions of libirecovery through a centralized compatibility layer. This document explains the differences and how the project handles them.

## API Differences

### Issue 1: Device ID Field Change

| Version | Field | Type | Notes |
|---------|-------|------|-------|
| **Legacy** (pre-2024) | `info->pid` | `uint32_t` | Product ID (old terminology) |
| **Modern** (2024+) | `info->cpid` | `uint32_t` | Chip ID (new terminology) |

**What changed:** Modern libirecovery removed the `pid` field entirely, replacing it with `cpid`. Both represent the device identifier.

**Error you'd see:** 
```
error: 'const struct irecv_device_info' has no member named 'pid'; did you mean 'cpid'?
```

### Issue 2: DFU Notify Flag Removal

| Version | Constant | Behavior |
|---------|----------|----------|
| **Legacy** (pre-2024) | `IRECV_SEND_OPT_DFU_NOTIFY_FINISH` | Explicit flag available |
| **Modern** (2024+) | *(removed)* | Must pass `0` as fallback |

**What changed:** The flag was removed as a named constant. Passing `0` has the same effect.

**Error you'd see:**
```
error: 'IRECV_SEND_OPT_DFU_NOTIFY_FINISH' undeclared (first use in this function)
```

## Solution: Compatibility Layer

### Header File: `include/compat/libirecovery_compat.h`

This header provides:

1. **`irecv_get_device_id(info)`** – Macro that returns the correct field based on available API
2. **`IRECV_DEVICE_ID_FIELD_NAME`** – String name for debug messages
3. **`IRECV_SEND_OPT_DFU_NOTIFY_FINISH`** – Defined as `0` if missing in modern API

### Usage Example

**Before (breaks on modern libirecovery):**
```c
if (!info || info->pid != APPLE_RECOVERY_PID) {
    log_error("Device PID mismatch: 0x%04X", info->pid);
    return -1;
}

irecv_send_file(client, path, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
```

**After (works on both):**
```c
#include "compat/libirecovery_compat.h"

uint32_t device_id = irecv_get_device_id(info);
if (!info || device_id != APPLE_RECOVERY_PID) {
    log_error("Device %s mismatch: 0x%04X", IRECV_DEVICE_ID_FIELD_NAME, device_id);
    return -1;
}

irecv_send_file(client, path, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
```

## Build Configuration

### Automatic Detection (Recommended)

The build system now auto-detects available libirecovery features:

```bash
make
```

The Makefile will:
1. Query `pkg-config` for libirecovery headers
2. Auto-detect whether `cpid` or `pid` is available
3. Check if `IRECV_SEND_OPT_DFU_NOTIFY_FINISH` exists
4. Apply appropriate compile flags automatically

**Build output example:**
```
libirecovery: cpid=yes dfu_notify=no
```

### Manual Override (If Needed)

If auto-detection fails or you need to force a specific API:

```bash
# Force legacy API (pid field)
make CFLAGS="-DHAVE_IRECV_DEVICE_INFO_PID"

# Force modern API (cpid field)
make CFLAGS="-DHAVE_IRECV_DEVICE_INFO_CPID"
```

## Files Modified

| File | Change |
|------|--------|
| `include/compat/libirecovery_compat.h` | **NEW** – Compatibility layer header |
| `src/bypass/path_b_identity.c` | Updated to use compat layer |
| `src/bypass/path_a_ramdisk.c` | Updated to use compat layer |
| `Makefile` | Added auto-detection logic |

## Tested Configurations

### ✅ Supported

- **Modern libirecovery** (2024+) – cpid field, no dfu_notify flag
- **Legacy libirecovery** (pre-2024) – pid field, dfu_notify flag available
- **Ubuntu 24.04 LTS** – Modern packages
- **Arch Linux** – Current repos (modern)
- **Older Debian/Ubuntu** – Legacy packages

### Installation Examples

**Modern (Ubuntu 24.04+):**
```bash
sudo apt install libirecovery-dev
```

**Legacy (Ubuntu 20.04, 22.04):**
```bash
sudo apt install libirecovery-dev  # Older version
```

**Arch Linux:**
```bash
sudo pacman -S libirecovery
```

## Verifying Your Setup

### Check libirecovery Version

```bash
pkg-config --modversion libirecovery-1.0
```

### Check Available API

```bash
# Check for cpid (modern)
grep -q "cpid" /usr/include/libirecovery.h && echo "Modern API (cpid)" || echo "Legacy API (pid)"

# Check for dfu_notify flag
grep -q "IRECV_SEND_OPT_DFU_NOTIFY_FINISH" /usr/include/libirecovery.h && \
    echo "DFU notify flag available" || echo "DFU notify flag missing"
```

## Troubleshooting

### Compilation fails with "no member named 'pid'"

This means your libirecovery is modern (2024+) but the compatibility layer isn't being used.

**Solution:**
```bash
make clean
make  # Rebuilds with auto-detection
```

### Compilation fails with "undeclared 'IRECV_SEND_OPT_DFU_NOTIFY_FINISH'"

The flag is missing, which is expected on modern libirecovery. The compatibility layer should handle this.

**Solution:**
```bash
# Verify the compat header is included
grep -r "libirecovery_compat.h" src/
# Should see entries in path_a_ramdisk.c and path_b_identity.c
```

### Build succeeds but crashes at runtime

Rare, but can occur if the libirecovery runtime library differs from headers used at compile time.

**Solution:**
```bash
# Check actual installed library
ldconfig -p | grep libirecovery

# Verify pkg-config paths
pkg-config --cflags --libs libirecovery-1.0
```

## Contributing

If you encounter compatibility issues with a different libirecovery version:

1. **Report the version:** `pkg-config --modversion libirecovery-1.0`
2. **Share the error message**
3. **Dump the header:** `pkg-config --cflags libirecovery-1.0 | tr ' ' '\n'`

Open an issue on GitHub with this information.

## References

- [libirecovery GitHub](https://github.com/libimobiledevice/libirecovery)
- [libimobiledevice Project](https://libimobiledevice.org/)
