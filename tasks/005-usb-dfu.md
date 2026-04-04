# Task 005: USB DFU Module

## Objective
Implement the USB DFU device detection and raw I/O module using libusb.
This finds Apple devices in DFU mode and provides low-level USB communication.

## Scope
**Files to create:**
- Itr4m/include/device/usb_dfu.h
- Itr4m/src/device/usb_dfu.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md
- Itr4m/include/util/usb_helpers.h (uses helpers from task 002)
- Itr4m/include/util/log.h

**Out of scope:**
- DFU protocol state machine (task 007)
- checkm8 exploit (task 010)

## Dependencies
- **Requires:** 001 (structure), 002 (usb_helpers)
- **Produces:** usb_dfu_find(), usb_dfu_open(), read serial/CPID/ECID from DFU

## Technical Details

### Constants
```c
#define APPLE_VID   0x05AC
#define DFU_PID     0x1227
#define RECOVERY_PID 0x1281
```

### API
```c
/* Find an Apple device in DFU mode. Returns 0 if found, -1 if not. */
int usb_dfu_find(libusb_device_handle **handle);

/* Read device serial string from DFU mode USB descriptor.
   Parses CPID, ECID, SRTG from the serial string.
   Format: "CPID:8015 CPRV:11 CPFM:03 SCEP:01 BDID:0E ECID:XXXX IBFL:3C SRTG:[iBoot-...]" */
int usb_dfu_read_info(libusb_device_handle *handle, uint32_t *cpid,
                      uint64_t *ecid, char *serial, size_t serial_len);

/* Send data to device via DFU DNLOAD */
int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len);

/* Receive data from device via DFU UPLOAD */
int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len, size_t *actual);

/* Close and release the DFU device */
void usb_dfu_close(libusb_device_handle *handle);

/* Initialize/cleanup libusb context */
int usb_dfu_init(void);
void usb_dfu_cleanup(void);
```

### Serial String Parsing
DFU mode devices expose a USB serial string descriptor containing
space-separated key:value pairs. Parse CPID and ECID as hex integers.

## Rules
- Read tasks/rules.md before starting
- Use usb_helpers.h for control transfers where possible
- Log all USB errors via log.h
- Max 300 lines

## Verification
- [ ] usb_dfu.c under 300 lines
- [ ] Proper libusb init/cleanup lifecycle
- [ ] Serial string parser handles missing fields gracefully

## Stop Conditions
- If libusb API differs from expected, note the version difference
