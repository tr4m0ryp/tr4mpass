# Task 002: Utility Modules

## Objective
Implement the three utility modules: logging, plist helpers, and USB helpers.
These are foundational utilities used by all other modules.

## Scope
**Files to create:**
- Itr4m/include/util/log.h
- Itr4m/src/util/log.c
- Itr4m/include/util/plist_helpers.h
- Itr4m/src/util/plist_helpers.c
- Itr4m/include/util/usb_helpers.h
- Itr4m/src/util/usb_helpers.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md

**Out of scope:**
- Makefile, itr4m.h (task 001)
- Any module-specific code

## Dependencies
- **Requires:** none (can compile independently)
- **Produces:** Utility headers/sources used by all other modules

## Technical Details

### log.h / log.c
Logging with levels. Simple implementation using fprintf to stderr.
```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;
void log_set_level(log_level_t level);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
```
Use ANSI color codes for terminal: white=debug, green=info, yellow=warn, red=error.
Prefix each line with `[level]` tag. Use variadic args (stdarg.h).

### plist_helpers.h / plist_helpers.c
Convenience wrappers around libplist for building activation plists.
```c
plist_t plist_new_dict_with_kvs(const char *key1, plist_t val1, ...);
char *plist_dict_get_string(plist_t dict, const char *key);
int plist_dict_get_bool(plist_t dict, const char *key, int *out);
char *plist_to_xml_string(plist_t plist, uint32_t *len);
plist_t plist_from_xml_string(const char *xml, uint32_t len);
```

### usb_helpers.h / usb_helpers.c
Shared USB control transfer wrappers for DFU operations.
```c
int usb_ctrl_transfer(libusb_device_handle *dev, uint8_t bmRequestType,
                      uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                      unsigned char *data, uint16_t wLength, unsigned int timeout);
int usb_ctrl_transfer_no_data(libusb_device_handle *dev, uint8_t bmRequestType,
                              uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                              unsigned int timeout);
void usb_print_error(int libusb_error);
```

## Rules
- Read tasks/rules.md before starting
- Only modify files listed in Scope
- No emojis
- Max 300 lines per file
- Each module must be self-contained (no cross-dependencies between utils)

## Verification
- [ ] Each .c file under 300 lines
- [ ] Headers have proper guards
- [ ] No cross-dependencies between the three util modules

## Stop Conditions
- If libplist or libusb headers are not available for syntax checking, note it and continue
