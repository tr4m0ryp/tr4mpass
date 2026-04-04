# Task 013: Path B -- A12+ Bypass

## Objective
Implement the A12+ bypass module that chains: identity manipulation ->
signal detection -> session activation -> deletescript -> verify.

## Scope
**Files to create:**
- Itr4m/include/bypass/path_b.h
- Itr4m/src/bypass/path_b.c
- Itr4m/src/bypass/path_b_identity.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (Path B flow)
- notes/Itr4m.md (Path B implementation hints, A12+ research)
- Itr4m/include/bypass/signal.h (from task 008)
- Itr4m/include/activation/session.h (from task 011)
- All relevant headers

**Out of scope:**
- Session protocol internals (task 011)
- Signal detection internals (task 008)
- Main.c orchestration (task 014)

## Dependencies
- **Requires:** 006 (bypass registry), 011 (session/record/deletescript)
- **Produces:** path_b_module (bypass_module_t) registered in main.c

## Technical Details

### path_b.h
```c
extern const bypass_module_t path_b_module;
```

### path_b.c -- orchestration
**probe()**: Returns 1 if dev->checkm8_vulnerable == 0 (A12+ device)
AND dev->is_dfu_mode == 1.

**execute()**: Runs the full A12+ flow:
1. path_b_manipulate_identity(dev) -- modify serial descriptors in DFU
2. signal_detect_type(dev) -- determine signal path
3. signal_print_info(dev) -- show user what was detected
4. session_get_info(dev, &session) -- get session blob
5. session_drm_handshake(dev, session, &response) -- handshake
6. session_create_activation_info(dev, response, &info) -- build info
7. record_build_a12(dev) -> session_activate(dev, record, response) -- activate
8. deletescript_run(dev) -- cleanup
9. activation_is_activated(dev) -- verify

### path_b_identity.c -- DFU identity manipulation
```c
/* Manipulate device serial/identity descriptors in DFU mode.
   Appends PWND:[checkm8] marker to serial string. */
int path_b_manipulate_identity(device_info_t *dev);

/* Read current serial descriptor from DFU mode */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len);

/* Write modified serial descriptor */
int path_b_write_serial(device_info_t *dev, const char *new_serial);
```

Identity manipulation works through USB control transfers to the DFU
interface. The device's serial descriptor is modified to include the
PWND marker which signals to the activation protocol that the device
has been prepared for bypass.

For v1, implement with correct USB control transfer structure but
mark the exact descriptor write offsets as TODO (these are determined
by testing with actual A12+ hardware).

## Rules
- Read tasks/rules.md before starting
- path_b.c: orchestration only
- path_b_identity.c: DFU identity manipulation
- Max 300 lines each
- Log each step clearly for debugging

## Verification
- [ ] path_b.c under 300 lines
- [ ] path_b_identity.c under 300 lines
- [ ] probe() correctly checks A12+ (checkm8_vulnerable == 0)
- [ ] execute() calls all steps in correct order
- [ ] path_b_module is a valid bypass_module_t

## Stop Conditions
- If identity manipulation requires undocumented USB operations, stub with TODO
