# Task 004: Device Module (Extended)

## Objective
Extend the existing device.h/c from research_icloud0/src/ with new fields
for CPID, ECID, MEID, cellular detection, and DFU mode awareness.

## Scope
**Files to create:**
- Itr4m/include/device/device.h
- Itr4m/src/device/device.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (device_info_t extended structure)
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/device.h
- /Users/macbookpro/projects/icloud_lock/research_icloud0/src/device.c

**Out of scope:**
- USB DFU detection (task 005)
- Chip database (task 003, but we use chip_db.h)

## Dependencies
- **Requires:** 001 (directory structure)
- **Produces:** Extended device_info_t used by all bypass and activation modules

## Technical Details
Start from the existing device.h/c. Extend device_info_t with:
- `uint32_t cpid` -- numeric chip ID, queried via lockdownd "ChipID"
- `uint64_t ecid` -- exclusive chip ID, queried via lockdownd "UniqueChipID"
- `char meid[128]` -- MEID for CDMA devices
- `char chip_name[32]` -- populated via chip_db_lookup()
- `int checkm8_vulnerable` -- set from chip_db
- `int is_cellular` -- 1 if IMEI or MEID present
- `int is_dfu_mode` -- 0 in this module (usb_dfu sets it)
- `libusb_device_handle *usb` -- NULL in this module (usb_dfu sets it)

New queries in device_query_info():
- "ChipID" -> cpid
- "UniqueChipID" -> ecid
- "MobileEquipmentIdentifier" -> meid

Call chip_db_lookup(cpid) to fill chip_name and checkm8_vulnerable.
Set is_cellular = (strlen(imei) > 0 || strlen(meid) > 0).

Keep all existing functions: device_detect, device_query_info, device_print_info, device_free.

## Rules
- Read tasks/rules.md before starting
- Preserve existing function signatures
- Add new fields at the END of the struct to maintain compatibility
- Max 300 lines per file

## Verification
- [ ] All existing device.h function signatures preserved
- [ ] New fields added without breaking existing layout
- [ ] device.c under 300 lines
- [ ] Includes chip_db.h for lookup

## Stop Conditions
- If existing device.c exceeds 300 lines after extension, split query logic into device_query.c
