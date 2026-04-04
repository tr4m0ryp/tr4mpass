# Task 006: Bypass Registry

## Objective
Carry forward the bypass module registry from research_icloud0/src/bypass.h/c
and add AFC utils. These are the plugin infrastructure for bypass strategies.

## Scope
**Files to create:**
- Itr4m/include/bypass/bypass.h
- Itr4m/src/bypass/bypass.c
- Itr4m/include/bypass/afc_utils.h
- Itr4m/src/bypass/afc_utils.c

**Files to read (not modify):**
- tasks/rules.md
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/bypass.h
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/bypass.c
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/afc_utils.h
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/afc_utils.c

**Out of scope:**
- Actual bypass modules (tasks 012, 013)

## Dependencies
- **Requires:** 001 (structure)
- **Produces:** bypass_module_t interface and AFC utils used by Path A and Path B

## Technical Details
Copy bypass.h/c and afc_utils.h/c from research_icloud0/src/, adjusting:
- Include paths to match new layout (e.g., "device/device.h" instead of "device.h")
- Header guards to match new file paths
- No functional changes to the bypass interface

The bypass_module_t interface: name, description, probe(), execute().
Registry: static array, bypass_register, bypass_select, bypass_execute, bypass_list, bypass_count.

AFC utils: afc_connect, afc_disconnect, afc_list_dir, afc_file_exists,
afc_read_file, afc_write_file.

## Rules
- Read tasks/rules.md before starting
- Preserve ALL existing function signatures exactly
- Only change include paths and header guards
- Max 300 lines per file

## Verification
- [ ] bypass.h function signatures match existing exactly
- [ ] afc_utils.h function signatures match existing exactly
- [ ] Include paths updated for new layout

## Stop Conditions
- If existing source exceeds 300 lines, split logically
