#ifndef CHIP_DB_TABLE_H
#define CHIP_DB_TABLE_H

/*
 * chip_db_table.h -- static chip table with real gaster offsets (part 1).
 *
 * Included by chip_db.c only. Do not include this file elsewhere.
 * All CPIDs, iBoot SRTG strings and memory offsets are sourced from
 * gaster (github.com/0x7ff/gaster) -- specifically the SecureROM
 * SRTG -> offset dispatch block in gaster.c:519-808. Each row carries
 * a "gaster.c:NNN" marker pointing at the SRTG string check that
 * assigns its offsets, so future audits can grep the source of truth.
 *
 * Marketing / .name strings are the human-readable identity of the
 * SoC and are cross-checked against theiphonewiki.com/wiki/CHIP. They
 * are the fields the operator sees on stdout, so they must match the
 * physical device -- see issue #53 for the historical mislabel bug.
 *
 * Part 2 (extended chips and A12+ entries) is in chip_db_table_ext.h,
 * which is included at the end of the array below.
 */

/* clang-format off */
static const chip_info_t g_chip_table[] = {

    /* ================================================================ */
    /* ARMv7 chips (32-bit) and 32-bit S-series watch SoCs              */
    /* ================================================================ */

    /* CPID 0x8950 -- S5L8950 = A6 (APL0598), iPhone 5 / iPhone 5c.
     * SRTG:[iBoot-1145.3]. gaster.c:519 */
    {
        .cpid = 0x8950,
        .name = "A6",
        .marketing = "iPhone 5 / iPhone 5c",
        .checkm8_compatible = 1,
        .config_large_leak    = 659,
        .config_overwrite_pad = 0x640,
        .insecure_memory_base = 0x10000000,
        .memcpy_addr          = 0x9ACC,
        .aes_crypto_cmd       = 0x7301,
        .gUSBSerialNumber     = 0x10061F80,
        .dfu_handle_request   = 0x10061A24,
        .usb_core_do_transfer = 0x7621,
        .dfu_handle_bus_reset = 0x10061A3C,
        .handle_interface_request          = 0x8161,
        .usb_create_string_descriptor      = 0x7C55,
        .usb_serial_number_string_descriptor = 0x100600D8,
        .payload_dest_armv7   = 0x10079800,
    },

    /* CPID 0x8955 -- S5L8955 = A6X (APL5598), iPad 4th gen.
     * SRTG:[iBoot-1145.3.3]. gaster.c:534 */
    {
        .cpid = 0x8955,
        .name = "A6X",
        .marketing = "iPad (4th gen)",
        .checkm8_compatible = 1,
        .config_large_leak    = 659,
        .config_overwrite_pad = 0x640,
        .insecure_memory_base = 0x10000000,
        .memcpy_addr          = 0x9B0C,
        .aes_crypto_cmd       = 0x7341,
        .gUSBSerialNumber     = 0x10061F80,
        .dfu_handle_request   = 0x10061A24,
        .usb_core_do_transfer = 0x7661,
        .dfu_handle_bus_reset = 0x10061A3C,
        .handle_interface_request          = 0x81A1,
        .usb_create_string_descriptor      = 0x7C95,
        .usb_serial_number_string_descriptor = 0x100600D8,
        .payload_dest_armv7   = 0x10079800,
    },

    /* CPID 0x8947 -- S5L8947 = A5 Rev B (APL2498), Apple TV 3rd gen rev A
     * (AppleTV3,2). Single-core 32 nm die shrink of A5.
     * SRTG:[iBoot-1458.2]. gaster.c:549 */
    {
        .cpid = 0x8947,
        .name = "S5L8947",
        .marketing = "Apple TV 3rd gen (rev A) (A5 Rev B)",
        .checkm8_compatible = 1,
        .config_large_leak    = 626,
        .config_overwrite_pad = 0x660,
        .insecure_memory_base = 0x34000000,
        .memcpy_addr          = 0x9A3C,
        .aes_crypto_cmd       = 0x7061,
        .gUSBSerialNumber     = 0x3402DDF8,
        .dfu_handle_request   = 0x3402D92C,
        .usb_core_do_transfer = 0x79ED,
        .dfu_handle_bus_reset = 0x3402D944,
        .handle_interface_request          = 0x7BC9,
        .usb_create_string_descriptor      = 0x72A9,
        .usb_serial_number_string_descriptor = 0x3402C2DA,
        .payload_dest_armv7   = 0x34039800,
    },

    /* CPID 0x7002 -- S1P (APL0778), Apple Watch Series 1 / Series 2.
     * SRTG:[iBoot-2098.0.0.2.4]. gaster.c:610 */
    {
        .cpid = 0x7002,
        .name = "S1P",
        .marketing = "Apple Watch Series 1 / Series 2",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x500,
        .insecure_memory_base = 0x46018000,
        .memcpy_addr          = 0x89F4,
        .aes_crypto_cmd       = 0x6341,
        .gUSBSerialNumber     = 0x46005958,
        .dfu_handle_request   = 0x46005898,
        .usb_core_do_transfer = 0x6E59,
        .dfu_handle_bus_reset = 0x460058B0,
        .handle_interface_request          = 0x7081,
        .usb_create_string_descriptor      = 0x6745,
        .usb_serial_number_string_descriptor = 0x4600034A,
        .payload_dest_armv7   = 0x46007800,
    },

    /* CPID 0x8002 -- T8002. Shared package family used by S1P/S2 (Watch
     * Series 1/2) and T1 (MacBook Pro Touch Bar controller).
     * SRTG:[iBoot-2651.0.0.1.31]. gaster.c:682 */
    {
        .cpid = 0x8002,
        .name = "S1P/S2/T1",
        .marketing = "Watch Series 1/2 / MacBook Pro Touch Bar (T1)",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x5C0,
        .config_hole          = 5,
        .insecure_memory_base = 0x48818000,
        .memcpy_addr          = 0xB6F8,
        .aes_crypto_cmd       = 0x86DD,
        .gUSBSerialNumber     = 0x48802AB8,
        .dfu_handle_request   = 0x48806344,
        .usb_core_do_transfer = 0x9411,
        .dfu_handle_bus_reset = 0x4880635C,
        .handle_interface_request          = 0x95F1,
        .usb_create_string_descriptor      = 0x8CA5,
        .usb_serial_number_string_descriptor = 0x4880037A,
        .payload_dest_armv7   = 0x48806E00,
    },

    /* CPID 0x8004 -- T8004 = S3, Apple Watch Series 3 (both GPS and
     * GPS+Cellular variants share this CPID).
     * SRTG:[iBoot-2651.0.0.3.3]. gaster.c:697 */
    {
        .cpid = 0x8004,
        .name = "S3",
        .marketing = "Apple Watch Series 3",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x5C0,
        .config_hole          = 5,
        .insecure_memory_base = 0x48818000,
        .memcpy_addr          = 0xA884,
        .aes_crypto_cmd       = 0x786D,
        .gUSBSerialNumber     = 0x48802AE8,
        .dfu_handle_request   = 0x48806384,
        .usb_core_do_transfer = 0x85A1,
        .dfu_handle_bus_reset = 0x4880639C,
        .handle_interface_request          = 0x877D,
        .usb_create_string_descriptor      = 0x7E35,
        .usb_serial_number_string_descriptor = 0x488003CA,
        .payload_dest_armv7   = 0x48806E00,
    },

    /* ================================================================ */
    /* ARM64 chips (A7 - A9 era, no redirect addresses needed)           */
    /* ================================================================ */

    /* CPID 0x8960 -- S5L8960 = A7 (APL0698), iPhone 5s / iPad Air /
     * iPad mini 2 / iPad mini 3. Apple's first ARM64 SoC.
     * SRTG:[iBoot-1704.10]. gaster.c:564 */
    {
        .cpid = 0x8960,
        .name = "A7",
        .marketing = "iPhone 5s / iPad Air / iPad mini 2 / iPad mini 3",
        .checkm8_compatible = 1,
        .config_large_leak    = 7936,
        .config_overwrite_pad = 0x5C0,
        .insecure_memory_base = 0x180380000,
        .patch_addr           = 0x100005CE0,
        .memcpy_addr          = 0x10000ED50,
        .aes_crypto_cmd       = 0x10000B9A8,
        .boot_tramp_end       = 0x1800E1000,
        .gUSBSerialNumber     = 0x180086CDC,
        .dfu_handle_request   = 0x180086C70,
        .usb_core_do_transfer = 0x10000CC78,
        .dfu_handle_bus_reset = 0x180086CA0,
        .handle_interface_request          = 0x10000CFB4,
        .usb_create_string_descriptor      = 0x10000BFEC,
        .usb_serial_number_string_descriptor = 0x180080562,
    },

    /* CPID 0x7000 -- S5L8960-derived A8 variant (APL1011), iPod touch 6G
     * / iPad mini 4.
     * SRTG:[iBoot-1992.0.0.1.19]. gaster.c:595 */
    {
        .cpid = 0x7000,
        .name = "A8",
        .marketing = "iPod touch 6G / iPad mini 4",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x500,
        .insecure_memory_base = 0x180380000,
        .patch_addr           = 0x100007E98,
        .memcpy_addr          = 0x100010E70,
        .aes_crypto_cmd       = 0x10000DA90,
        .boot_tramp_end       = 0x1800E1000,
        .gUSBSerialNumber     = 0x1800888C8,
        .dfu_handle_request   = 0x180088878,
        .usb_core_do_transfer = 0x10000EBB4,
        .dfu_handle_bus_reset = 0x180088898,
        .handle_interface_request          = 0x10000EEE4,
        .usb_create_string_descriptor      = 0x10000E074,
        .usb_serial_number_string_descriptor = 0x18008062A,
    },

    /* CPID 0x7001 -- A8X (APL1012), iPad Air 2.
     * SRTG:[iBoot-1991.0.0.2.16]. gaster.c:580 */
    {
        .cpid = 0x7001,
        .name = "A8X",
        .marketing = "iPad Air 2",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x500,
        .insecure_memory_base = 0x180380000,
        .patch_addr           = 0x10000AD04,
        .memcpy_addr          = 0x100013F10,
        .aes_crypto_cmd       = 0x100010A90,
        .boot_tramp_end       = 0x1800E1000,
        .gUSBSerialNumber     = 0x180088E48,
        .dfu_handle_request   = 0x180088DF8,
        .usb_core_do_transfer = 0x100011BB4,
        .dfu_handle_bus_reset = 0x180088E18,
        .handle_interface_request          = 0x100011EE4,
        .usb_create_string_descriptor      = 0x100011074,
        .usb_serial_number_string_descriptor = 0x180080C2A,
    },

    /* CPID 0x8000 -- S8000 = A9 Samsung 14 nm (APL0898), iPhone 6s /
     * iPhone 6s Plus / iPhone SE (1st gen).
     * SRTG:[iBoot-2234.0.0.3.3]. gaster.c:641 */
    {
        .cpid = 0x8000,
        .name = "A9",
        .marketing = "iPhone 6s / 6s Plus / SE (Samsung 14 nm, APL0898)",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x500,
        .insecure_memory_base = 0x180380000,
        .patch_addr           = 0x10000812C,
        .memcpy_addr          = 0x100011030,
        .aes_crypto_cmd       = 0x10000DAA0,
        .boot_tramp_end       = 0x1800E1000,
        .gUSBSerialNumber     = 0x180087958,
        .dfu_handle_request   = 0x1800878F8,
        .usb_core_do_transfer = 0x10000EE78,
        .dfu_handle_bus_reset = 0x180087928,
        .handle_interface_request          = 0x10000F1B0,
        .usb_create_string_descriptor      = 0x10000E354,
        .usb_serial_number_string_descriptor = 0x1800807DA,
        .ttbr0_addr           = 0x1800C8000,
        .ttbr0_vrom_off       = 0x400,
    },

    /* CPID 0x8003 -- S8003 = A9 TSMC 16 nm (APL1022), iPhone 6s /
     * iPhone 6s Plus / iPhone SE (1st gen). Offset payload matches
     * 0x8000; only the foundry-identifying CPID differs.
     * SRTG:[iBoot-2234.0.0.2.22]. gaster.c:624 */
    {
        .cpid = 0x8003,
        .name = "A9",
        .marketing = "iPhone 6s / 6s Plus / SE (TSMC 16 nm, APL1022)",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x500,
        .insecure_memory_base = 0x180380000,
        .patch_addr           = 0x10000812C,
        .memcpy_addr          = 0x100011030,
        .aes_crypto_cmd       = 0x10000DAA0,
        .boot_tramp_end       = 0x1800E1000,
        .gUSBSerialNumber     = 0x180087958,
        .dfu_handle_request   = 0x1800878F8,
        .usb_core_do_transfer = 0x10000EE78,
        .dfu_handle_bus_reset = 0x180087928,
        .handle_interface_request          = 0x10000F1B0,
        .usb_create_string_descriptor      = 0x10000E354,
        .usb_serial_number_string_descriptor = 0x1800807DA,
        .ttbr0_addr           = 0x1800C8000,
        .ttbr0_vrom_off       = 0x400,
    },

/* Part 2 continues in chip_db_table_ext.h (extended chips + A12+ entries) */
#include "device/chip_db_table_ext.h"

#endif /* CHIP_DB_TABLE_H */
