# Task 012: Path A -- A5-A11 Bypass

## Objective
Implement the A5-A11 bypass module that chains: checkm8 exploit -> ramdisk
load -> jailbreak -> activation bypass -> deletescript -> verify.

## Scope
**Files to create:**
- Itr4m/include/bypass/path_a.h
- Itr4m/src/bypass/path_a.c
- Itr4m/src/bypass/path_a_jailbreak.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (Path A flow)
- notes/Itr4m.md (Path A implementation hints)
- All headers from tasks 003-011

**Out of scope:**
- checkm8 exploit internals (task 010)
- Activation protocol internals (tasks 009, 011)
- Main.c orchestration (task 014)

## Dependencies
- **Requires:** 006 (bypass registry), 010 (checkm8)
- **Produces:** path_a_module (bypass_module_t) registered in main.c

## Technical Details

### path_a.h
```c
/* The Path A bypass module for registration */
extern const bypass_module_t path_a_module;
```

### path_a.c -- orchestration
Implements probe() and execute() for bypass_module_t:

**probe()**: Returns 1 if dev->checkm8_vulnerable == 1 AND dev->is_dfu_mode == 1.

**execute()**: Runs the full A5-A11 flow:
1. checkm8_exploit(dev) -- DFU exploit
2. checkm8_verify_pwned(dev) -- verify PWND in serial
3. path_a_load_ramdisk(dev) -- send ramdisk via DFU
4. path_a_jailbreak(dev) -- apply kernel patches
5. record_build(dev) -> activation_submit_record(dev, record) -- activate
6. deletescript_run(dev) -- cleanup
7. activation_is_activated(dev) -- verify

### path_a_jailbreak.c -- post-exploit setup
```c
/* Load ramdisk from payloads/ directory via pwned DFU */
int path_a_load_ramdisk(device_info_t *dev);

/* Apply jailbreak kernel patches via ramdisk SSH */
int path_a_jailbreak(device_info_t *dev);

/* Replace mobileactivationd with patched version */
int path_a_replace_mobileactivationd(device_info_t *dev);
```

The jailbreak step communicates with the ramdisk via SSH (using the device's
USB-tunneled connection). For v1, implement the function signatures with
logging and TODO markers for the actual SSH commands -- the ramdisk-specific
commands depend on which ramdisk we use (palera1n vs jbinit).

## Rules
- Read tasks/rules.md before starting
- path_a.c: orchestration only, delegates to helpers
- path_a_jailbreak.c: ramdisk + jailbreak specifics
- Max 300 lines each
- Use log_info/log_error for progress reporting

## Verification
- [ ] path_a.c under 300 lines
- [ ] path_a_jailbreak.c under 300 lines
- [ ] probe() correctly checks checkm8_vulnerable and is_dfu_mode
- [ ] execute() calls all steps in correct order
- [ ] path_a_module is a valid bypass_module_t

## Stop Conditions
- If ramdisk loading requires features not yet implemented, stub with TODO
