# Task 003: Chip Database

## Objective
Implement the chip database module mapping Apple CPID values to chip names,
checkm8 vulnerability status, and exploit-specific offsets.

## Scope
**Files to create:**
- Itr4m/include/device/chip_db.h
- Itr4m/src/device/chip_db.c

**Files to read (not modify):**
- tasks/rules.md
- notes/architecture.md (chip_info_t structure)

**Out of scope:**
- Exploit payloads (task 010)
- Device detection code (task 004)

## Dependencies
- **Requires:** none
- **Produces:** chip_info_t type and chip_db_lookup() used by device and exploit modules

## Technical Details

### chip_info_t
```c
typedef struct {
    uint32_t cpid;
    const char *name;           /* "A7", "A8", ..., "A17" */
    const char *marketing;      /* "iPhone 5s", "iPhone X", etc. */
    int checkm8_vulnerable;     /* 1 for A5-A11, 0 for A12+ */
    uint32_t heap_base;         /* DFU heap base (0 if not vulnerable) */
    uint32_t payload_base;      /* Shellcode addr (0 if not vulnerable) */
} chip_info_t;
```

### API
```c
const chip_info_t *chip_db_lookup(uint32_t cpid);
const char *chip_db_name(uint32_t cpid);          /* returns name or "Unknown" */
int chip_db_is_checkm8_vulnerable(uint32_t cpid);  /* 1/0 */
void chip_db_print_all(void);                      /* debug: list all known chips */
```

### Known CPIDs (from gaster and public sources)
Include at minimum these chips:
- 0x8947: A7 (iPhone 5s)
- 0x8960: A8 (iPhone 6)
- 0x7000: A8 (variant)
- 0x7001: A8X
- 0x8000/0x8003: A9
- 0x8001: A9X
- 0x8010: A10 (iPhone 7)
- 0x8011: A10X
- 0x8015: A11 (iPhone 8/X)
- 0x8020: A12 (iPhone XS)
- 0x8027: A12Z
- 0x8030: A13 (iPhone 11)
- 0x8101: A14 (iPhone 12)
- 0x8103: M1
- 0x8110: A15 (iPhone 13)
- 0x8112: M2
- 0x8120: A16 (iPhone 14 Pro)
- 0x8130: A17 (iPhone 15 Pro)

For A5-A11 entries, fill heap_base and payload_base with values from gaster
source (0x7ff/gaster on GitHub). Use 0 for A12+ entries.

## Rules
- Read tasks/rules.md before starting
- Pure data module -- no I/O, no library dependencies except standard C
- Static const array for the chip table
- Null-terminated sentinel entry at end of table

## Verification
- [ ] chip_db.c under 300 lines
- [ ] All CPIDs listed above are present
- [ ] chip_db_lookup returns NULL for unknown CPIDs
- [ ] Compiles standalone: `cc -std=c99 -Iinclude -c src/device/chip_db.c`

## Stop Conditions
- If CPID values cannot be verified from public sources, use best available and note uncertainty
