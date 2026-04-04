# Task 009: Activation Client

## Objective
Extend the existing activation.h/c from research_icloud0/src/ to include
the full mobileactivation service client. This wraps libimobiledevice's
mobileactivation API for querying state and submitting records.

## Scope
**Files to create:**
- Itr4m/include/activation/activation.h
- Itr4m/src/activation/activation.c

**Files to read (not modify):**
- tasks/rules.md
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/activation.h
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/activation.c
- notes/Itr4m.md (activation record format, drmHandshake protocol)

**Out of scope:**
- Session mode protocol (task 011)
- Record building (task 011)

## Dependencies
- **Requires:** 004 (device module)
- **Produces:** Activation client used by session.c and bypass modules

## Technical Details
Start from existing activation.h/c. Carry forward all 4 existing functions:
- activation_get_state()
- activation_get_session_info()
- activation_get_info()
- activation_submit_record()

Add new functions:
```c
/* Submit activation record with session headers (iOS 11.2+ requirement).
   session_headers is the plist from the drmHandshake response. */
int activation_submit_record_with_session(device_info_t *dev,
    const char *record_xml, uint32_t record_len,
    plist_t session_headers);

/* Deactivate the device (reset activation state) */
int activation_deactivate(device_info_t *dev);

/* Check if device is activated. Returns 1=yes, 0=no, -1=error */
int activation_is_activated(device_info_t *dev);
```

Update include paths for new layout ("device/device.h").

## Rules
- Read tasks/rules.md before starting
- Preserve all existing function signatures
- Add new functions, do not remove or modify existing ones
- Max 300 lines

## Verification
- [ ] All 4 existing functions preserved with same signatures
- [ ] New functions added
- [ ] activation.c under 300 lines
- [ ] Uses mobileactivation.h from libimobiledevice

## Stop Conditions
- If activation.c approaches 300 lines, split into activation_query.c and activation_submit.c
