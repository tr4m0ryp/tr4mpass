# Task 008: Signal Detection

## Objective
Implement cellular vs WiFi-only detection and signal path selection.
Determines whether the bypass should preserve cellular or go WiFi-only.

## Scope
**Files to create:**
- Itr4m/include/bypass/signal.h
- Itr4m/src/bypass/signal.c

**Files to read (not modify):**
- tasks/rules.md
- notes/Itr4m.md (D5: signal vs no-signal, MEID vs GSM research)
- Itr4m/include/device/device.h

**Out of scope:**
- Actual baseband manipulation (done in path_b)
- Activation protocol (task 009)

## Dependencies
- **Requires:** 004 (device with IMEI/MEID fields)
- **Produces:** Signal detection functions used by Path B

## Technical Details

### signal_type_t
```c
typedef enum {
    SIGNAL_NONE,     /* WiFi-only device (iPad WiFi, iPod) */
    SIGNAL_GSM,      /* GSM device (IMEI present, no MEID) */
    SIGNAL_MEID,     /* CDMA device (MEID present) */
    SIGNAL_DUAL      /* Both IMEI and MEID present */
} signal_type_t;
```

### API
```c
/* Detect signal type from device info */
signal_type_t signal_detect_type(const device_info_t *dev);

/* Check if signal preservation is possible for this device */
int signal_can_preserve(const device_info_t *dev);

/* Get carrier identifier for activation record.
   Returns MEID if available (more reliable), IMEI otherwise.
   Writes to buf, returns 0 on success. */
int signal_get_carrier_id(const device_info_t *dev, char *buf, size_t len);

/* Print signal detection results */
void signal_print_info(const device_info_t *dev);
```

### Logic
- If neither IMEI nor MEID: SIGNAL_NONE (WiFi-only)
- If MEID present: prefer MEID path (more reliable per research)
- If only IMEI: GSM path
- signal_can_preserve: returns 1 for MEID/GSM/DUAL, 0 for NONE

## Rules
- Read tasks/rules.md before starting
- Simple detection logic -- no network operations
- Max 300 lines (should be well under)

## Verification
- [ ] All signal_type_t values handled
- [ ] WiFi-only devices correctly detected (no IMEI, no MEID)
- [ ] signal.c under 300 lines

## Stop Conditions
- None expected
