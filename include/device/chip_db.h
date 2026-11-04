#ifndef CHIP_DB_H
#define CHIP_DB_H

#include <stdint.h>

/*
 * chip_info_t -- static descriptor for a known Apple SoC variant.
 *
 * All memory addresses and stage parameters are sourced from gaster
 * (github.com/0x7ff/gaster). Fields are 0 when unused for a given chip.
 */
typedef struct {
    uint32_t    cpid;
    const char *name;               /* "A7", "A8", ..., "A17"            */
    const char *marketing;          /* "iPhone 5s", "iPhone X", etc.     */
    int         checkm8_compatible; /* 1 = A5-A11, 0 = A12+             */

    /* Stage config */
    uint16_t config_large_leak;     /* allocation iterations (0 = no-leak path) */
    uint16_t config_overwrite_pad;  /* overwrite padding size                   */
    uint16_t config_hole;           /* hole count for newer chips (0 if unused) */

    /* Memory addresses */
    uint64_t insecure_memory_base;
    uint64_t patch_addr;
    uint64_t memcpy_addr;
    uint64_t aes_crypto_cmd;
    uint64_t boot_tramp_end;
    uint64_t gUSBSerialNumber;
    uint64_t dfu_handle_request;
    uint64_t usb_core_do_transfer;
    uint64_t dfu_handle_bus_reset;
    uint64_t handle_interface_request;
    uint64_t usb_create_string_descriptor;
    uint64_t usb_serial_number_string_descriptor;

    /* Redirect addresses (A10+ only, 0 for older) */
    uint64_t ttbr0_addr;
    uint64_t tlbi;
    uint64_t nop_addr;
    uint64_t ret_addr;
    uint64_t func_addr;
    uint64_t write_ttbr0;
    uint64_t ttbr0_vrom_off;
    uint64_t ttbr0_sram_off;

    /* ARMv7 payload dest (0 for ARM64 chips) */
    uint64_t payload_dest_armv7;
} chip_info_t;

/*
 * chip_db_lookup -- return chip_info_t for the given CPID, or NULL if
 *                   the CPID is not in the table.
 */
const chip_info_t *chip_db_lookup(uint32_t cpid);

/*
 * chip_db_name -- return the short chip name ("A10", "A11", ...) for the
 *                 given CPID, or "Unknown" if not found.
 */
const char *chip_db_name(uint32_t cpid);

/*
 * chip_db_is_checkm8_compatible -- return 1 if the CPID is checkm8-
 *                                  compatible (A5-A11), 0 otherwise.
 */
int chip_db_is_checkm8_compatible(uint32_t cpid);

/*
 * chip_db_print_all -- print the full chip table to stdout (debug only).
 */
void chip_db_print_all(void);

#endif /* CHIP_DB_H */
