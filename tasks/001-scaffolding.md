# Task 001: Project Scaffolding

## Objective
Create the Itr4m directory structure, Makefile, and top-level header (itr4m.h).
This establishes the build system and directory layout all other tasks build on.

## Scope
**Files to create:**
- Itr4m/Makefile
- Itr4m/include/itr4m.h

**Directories to create:**
- Itr4m/include/device/
- Itr4m/include/exploit/
- Itr4m/include/bypass/
- Itr4m/include/activation/
- Itr4m/include/util/
- Itr4m/src/device/
- Itr4m/src/exploit/
- Itr4m/src/exploit/payload/
- Itr4m/src/bypass/
- Itr4m/src/activation/
- Itr4m/src/util/
- Itr4m/payloads/
- Itr4m/scripts/

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (Build System section)

**Out of scope:**
- Any .c source files (other tasks handle those)
- start.sh (task 014)

## Dependencies
- **Requires:** none
- **Produces:** Directory structure and Makefile that all other tasks depend on

## Technical Details

### itr4m.h
Top-level header with:
- Version string: `#define ITR4M_VERSION "0.1.0"`
- Common return codes: `#define ITR4M_OK 0`, `#define ITR4M_ERR -1`
- Common includes: stdint.h, stddef.h, stdio.h, stdlib.h, string.h

### Makefile
See architecture.md Build System section. Key points:
- CC = cc
- CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2
- pkg-config for: libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 libplist-2.0 openssl
- SRCS = $(shell find src -name '*.c')
- -Iinclude for header path
- Target: itr4m

### Directories
Create all directories listed in architecture.md layout. Use .gitkeep in
empty directories so git tracks them.

## Rules
- Read tasks/rules.md before starting
- Only create files listed in Scope
- No emojis
- Makefile must handle the case where pkg-config libs are not installed (warn, not fail)

## Verification
- [ ] `make clean` runs without error
- [ ] Directory structure matches architecture.md
- [ ] itr4m.h compiles: `cc -Iinclude -fsyntax-only -c -xc - <<< '#include "itr4m.h"'`

## Stop Conditions
- If architecture.md is missing, stop and report
