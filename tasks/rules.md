# Itr4m -- Shared Build Rules

## Language and Standards
- C99 with POSIX extensions (`-D_GNU_SOURCE`)
- Compiler: `cc` (clang on macOS)
- Flags: `-Wall -Wextra -std=c99 -D_GNU_SOURCE -O2`

## File Rules
- Maximum 300 lines per source file. Split proactively at ~200.
- No emojis in any file, comment, or output. Ever.
- No .md files except in notes/ and tasks/ directories.
- Headers go in include/, sources in src/, mirroring subdirectory structure.

## Naming Conventions
- Files: lowercase_with_underscores.c/.h
- Functions: module_action() pattern (e.g., device_detect, chip_db_lookup)
- Types: module_type_t suffix (e.g., device_info_t, chip_info_t)
- Constants: UPPER_SNAKE_CASE
- Includes: use angle brackets for system/library headers, quotes for project

## Header Guards
Use #ifndef pattern: `#ifndef MODULE_NAME_H` / `#define MODULE_NAME_H`

## Error Handling
- Return 0 on success, -1 on error (match existing device.h/activation.h)
- Use util/log.h for messages: log_info(), log_warn(), log_error()
- Always check return values from library calls

## Dependencies (via pkg-config)
- libimobiledevice-1.0 (device comms, lockdownd, mobileactivation)
- libirecovery-1.0 (DFU/recovery mode)
- libusb-1.0 (raw USB for DFU exploit)
- libplist-2.0 (plist construction/parsing)
- openssl (crypto)

## Include Paths
- All includes relative to include/ directory
- Example: `#include "device/device.h"` not `#include "../include/device/device.h"`
- The Makefile passes `-Iinclude` to the compiler

## Existing Interfaces (DO NOT CHANGE signatures)
These types and function signatures are carried from research_icloud0/src/.
New code must be compatible with them:

### device_info_t (extend, do not break)
Existing fields: udid, product_type, chip_id, ios_version, activation_state,
serial, imei, hardware_model, device_name, handle, lockdown.
New fields to add: cpid (uint32_t), ecid (uint64_t), chip_name, meid,
checkm8_vulnerable, is_cellular, is_dfu_mode, usb (libusb handle).

### bypass_module_t (unchanged)
Fields: name[64], description[256], probe(), execute().
probe() returns 1=compatible, 0=not, -1=error.
execute() returns 0=success, non-zero=failure.

### activation API (extend, do not break)
Existing: activation_get_state, activation_get_session_info,
activation_get_info, activation_submit_record.
New: session_*, record_* functions in separate files.

## Build Verification
After writing code, verify with:
```
cd /Users/macbookpro/projects/icloud_lock/Itr4m && make clean && make 2>&1
```
If deps are missing, the build will fail at pkg-config. That is expected
on systems without the libraries installed. Code should be syntactically
correct and structurally sound regardless.

## Project Root
/Users/macbookpro/projects/icloud_lock/Itr4m/

## Reference Code
/Users/macbookpro/projects/icloud_lock/research_icloud0/src/ -- existing framework
