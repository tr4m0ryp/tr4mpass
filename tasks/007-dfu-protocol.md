# Task 007: DFU Protocol

## Objective
Implement the DFU protocol state machine -- the low-level protocol operations
(DNLOAD, UPLOAD, GETSTATUS, CLRSTATUS, ABORT) used by the checkm8 exploit.

## Scope
**Files to create:**
- Itr4m/include/exploit/dfu_proto.h
- Itr4m/src/exploit/dfu_proto.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md
- Itr4m/include/device/usb_dfu.h (from task 005)
- Itr4m/include/util/usb_helpers.h

**Out of scope:**
- checkm8 exploit logic (task 010)
- Payload data (task 010)

## Dependencies
- **Requires:** 005 (usb_dfu)
- **Produces:** DFU protocol operations used by checkm8.c

## Technical Details

### DFU Request Types
```c
#define DFU_DETACH      0x00
#define DFU_DNLOAD      0x01
#define DFU_UPLOAD      0x02
#define DFU_GETSTATUS   0x03
#define DFU_CLRSTATUS   0x04
#define DFU_GETSTATE    0x05
#define DFU_ABORT       0x06
```

### DFU Status Structure
```c
typedef struct {
    uint8_t  bStatus;
    uint32_t bwPollTimeout;  /* 3 bytes in protocol, stored as uint32 */
    uint8_t  bState;
    uint8_t  iString;
} dfu_status_t;
```

### API
```c
int dfu_get_status(libusb_device_handle *dev, dfu_status_t *status);
int dfu_clr_status(libusb_device_handle *dev);
int dfu_get_state(libusb_device_handle *dev, uint8_t *state);
int dfu_dnload(libusb_device_handle *dev, uint16_t block_num,
               const void *data, size_t len);
int dfu_upload(libusb_device_handle *dev, uint16_t block_num,
               void *buf, size_t len, size_t *actual);
int dfu_abort(libusb_device_handle *dev);

/* Higher-level: send data and wait for status */
int dfu_send_data(libusb_device_handle *dev, const void *data, size_t len);

/* Reset DFU state to idle */
int dfu_reset_to_idle(libusb_device_handle *dev);
```

All control transfers use bmRequestType = 0x21 (host-to-device, class, interface)
for OUT requests and 0xA1 for IN requests.

## Rules
- Read tasks/rules.md before starting
- Pure protocol implementation -- no exploit logic
- Log all protocol errors
- Max 300 lines

## Verification
- [ ] All DFU request types implemented
- [ ] Proper byte ordering for status parsing (little-endian wire format)
- [ ] dfu_proto.c under 300 lines

## Stop Conditions
- None expected -- this is a well-defined protocol
