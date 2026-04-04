# Task 014: Main Orchestration + start.sh

## Objective
Implement main.c (CLI entry point, module registration, device detection
orchestration) and start.sh (guided wrapper script).

## Scope
**Files to create:**
- Itr4m/src/main.c
- Itr4m/start.sh

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (main.c orchestration flow, start.sh flow)
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/main.c
- All include/ headers from prior tasks

**Out of scope:**
- Any module internals (all delegated to modules)

## Dependencies
- **Requires:** 012 (Path A), 013 (Path B) -- all modules must exist
- **Produces:** The final itr4m binary entry point and start.sh

## Technical Details

### main.c
Based on research_icloud0/src/main.c but fully rewritten for Itr4m:

```
1. print_banner() -- "Itr4m v0.1.0"
2. parse_args() -- --verbose, --dry-run, --detect-only, --force-path-a, --force-path-b
3. usb_dfu_init() -- initialize libusb
4. Device detection:
   a. usb_dfu_find() -- try DFU mode first
   b. If DFU found: populate dev from USB serial (CPID, ECID)
   c. If no DFU: device_detect() via libimobiledevice, then device_query_info()
5. chip_db_lookup(dev.cpid) -- fill chip_name, checkm8_vulnerable
6. device_print_info(&dev) -- show device summary
7. If --detect-only: exit here
8. Register modules: bypass_register(&path_a_module), bypass_register(&path_b_module)
9. bypass_select(&dev) -- or use forced path if specified
10. bypass_execute(mod, &dev) -- run selected path
11. Print result
12. device_free(&dev), usb_dfu_cleanup()
```

CLI args: simple getopt loop, no external arg parsing library.

### start.sh
See architecture.md start.sh section. Key features:
- Check dependencies (itr4m binary, brew packages)
- Detect device (normal mode or DFU)
- If no device: guide user into DFU mode with step-by-step instructions
- Run ./itr4m with any passed arguments
- Make executable: chmod +x

## Rules
- Read tasks/rules.md before starting
- main.c: orchestration ONLY, no business logic
- Max 300 lines for main.c
- start.sh: plain bash, no fancy dependencies
- No emojis in any output (banner, messages, etc.)

## Verification
- [ ] main.c under 300 lines
- [ ] --detect-only flag works (early exit after device info)
- [ ] --verbose sets log level to DEBUG
- [ ] Both path_a_module and path_b_module are registered
- [ ] start.sh is executable and has correct shebang
- [ ] No emojis in any output

## Stop Conditions
- If a required header is missing, stub the include and note it
