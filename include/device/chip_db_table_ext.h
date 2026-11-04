/*
 * chip_db_table_ext.h -- chip table part 2: redirect-capable chips + A12+ stubs.
 *
 * Included by chip_db_table.h inside the g_chip_table[] array.
 * Do not include this file directly. All offsets from gaster.c
 * (github.com/0x7ff/gaster). Each row carries a "gaster.c:NNN" marker
 * pointing at the SRTG string check that owns those offsets.
 */
#ifndef CHIP_DB_TABLE_EXT_H
#define CHIP_DB_TABLE_EXT_H

    /* ================================================================ */
    /* ARM64 chips with redirect addresses (A9X, A10, A10X, A11, T2)          */
    /* ================================================================ */

    /* CPID 0x8001 -- S8001 = A9X (APL1021), iPad Pro (1st gen).
     * SRTG:[iBoot-2481.0.0.2.1]. gaster.c:658 */
    {
        .cpid = 0x8001,
        .name = "A9X",
        .marketing = "iPad Pro (1st gen)",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x5C0,
        .config_hole          = 6,
        .insecure_memory_base = 0x180000000,
        .patch_addr           = 0x100007668,
        .memcpy_addr          = 0x1000106F0,
        .aes_crypto_cmd       = 0x10000C9D4,
        .boot_tramp_end       = 0x180044000,
        .gUSBSerialNumber     = 0x180047578,
        .dfu_handle_request   = 0x18004C378,
        .usb_core_do_transfer = 0x10000DDA4,
        .dfu_handle_bus_reset = 0x18004C3A8,
        .handle_interface_request          = 0x10000E0B4,
        .usb_create_string_descriptor      = 0x10000D280,
        .usb_serial_number_string_descriptor = 0x18004486A,
        .ttbr0_addr           = 0x180050000,
        .tlbi                 = 0x100000404,
        .nop_addr           = 0x10000CD60,
        .ret_addr           = 0x100000118,
        .func_addr          = 0x10000CD40,
        .write_ttbr0          = 0x1000003B4,
        .ttbr0_vrom_off       = 0x400,
        .ttbr0_sram_off       = 0x600,
    },

    /* CPID 0x8010 -- T8010 = A10 Fusion (APL1W24), iPhone 7 / iPhone 7
     * Plus / iPod touch 7 / iPad (6th gen) / iPad (7th gen).
     * SRTG:[iBoot-2696.0.0.1.33]. gaster.c:712 */
    {
        .cpid = 0x8010,
        .name = "A10",
        .marketing = "iPhone 7/7+ / iPod touch 7 / iPad 6 / iPad 7",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x5C0,
        .config_hole          = 5,
        .insecure_memory_base = 0x1800B0000,
        .patch_addr           = 0x1000074AC,
        .memcpy_addr          = 0x100010730,
        .aes_crypto_cmd       = 0x10000C8F4,
        .boot_tramp_end       = 0x1800B0000,
        .gUSBSerialNumber     = 0x180083CF8,
        .dfu_handle_request   = 0x180088B48,
        .usb_core_do_transfer = 0x10000DC98,
        .dfu_handle_bus_reset = 0x180088B78,
        .handle_interface_request          = 0x10000DFB8,
        .usb_create_string_descriptor      = 0x10000D150,
        .usb_serial_number_string_descriptor = 0x1800805DA,
        .ttbr0_addr           = 0x1800A0000,
        .tlbi                 = 0x100000434,
        .nop_addr           = 0x10000CC6C,
        .ret_addr           = 0x10000015C,
        .func_addr          = 0x10000CC4C,
        .write_ttbr0          = 0x1000003E4,
        .ttbr0_vrom_off       = 0x400,
        .ttbr0_sram_off       = 0x600,
    },

    /* CPID 0x8011 -- T8011 = A10X Fusion (APL1071), iPad Pro 10.5" /
     * iPad Pro 12.9" (2nd gen) / Apple TV 4K (1st gen).
     * SRTG:[iBoot-3135.0.0.2.3]. gaster.c:736 */
    {
        .cpid = 0x8011,
        .name = "A10X",
        .marketing = "iPad Pro 10.5\" / iPad Pro 12.9\" (2nd gen) / Apple TV 4K",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x540,
        .config_hole          = 6,
        .insecure_memory_base = 0x1800B0000,
        .patch_addr           = 0x100007630,
        .memcpy_addr          = 0x100010950,
        .aes_crypto_cmd       = 0x10000C994,
        .boot_tramp_end       = 0x1800B0000,
        .gUSBSerialNumber     = 0x180083D28,
        .dfu_handle_request   = 0x180088A58,
        .usb_core_do_transfer = 0x10000DD64,
        .dfu_handle_bus_reset = 0x180088A88,
        .handle_interface_request          = 0x10000E08C,
        .usb_create_string_descriptor      = 0x10000D234,
        .usb_serial_number_string_descriptor = 0x18008062A,
        .ttbr0_addr           = 0x1800A0000,
        .tlbi                 = 0x100000444,
        .nop_addr           = 0x10000CD0C,
        .ret_addr           = 0x100000148,
        .func_addr          = 0x10000CCEC,
        .write_ttbr0          = 0x1000003F4,
        .ttbr0_vrom_off       = 0x400,
        .ttbr0_sram_off       = 0x600,
    },

    /* CPID 0x8015 -- T8015 = A11 Bionic (APL1W72), iPhone 8 /
     * iPhone 8 Plus / iPhone X.
     * SRTG:[iBoot-3332.0.0.1.23]. gaster.c:760 */
    {
        .cpid = 0x8015,
        .name = "A11",
        .marketing = "iPhone 8 / iPhone 8 Plus / iPhone X",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x540,
        .config_hole          = 6,
        .insecure_memory_base = 0x18001C000,
        .patch_addr           = 0x10000624C,
        .memcpy_addr          = 0x10000E9D0,
        .aes_crypto_cmd       = 0x100009E9C,
        .boot_tramp_end       = 0x18001C000,
        .gUSBSerialNumber     = 0x180003A78,
        .dfu_handle_request   = 0x180008638,
        .usb_core_do_transfer = 0x10000B9A8,
        .dfu_handle_bus_reset = 0x180008668,
        .handle_interface_request          = 0x10000BCCC,
        .usb_create_string_descriptor      = 0x10000AE80,
        .usb_serial_number_string_descriptor = 0x1800008FA,
        .ttbr0_addr           = 0x18000C000,
        .tlbi                 = 0x1000004AC,
        .nop_addr           = 0x10000A9C4,
        .ret_addr           = 0x100000148,
        .func_addr          = 0x10000A9AC,
        .write_ttbr0          = 0x10000045C,
        .ttbr0_vrom_off       = 0x400,
        .ttbr0_sram_off       = 0x600,
    },

    /* CPID 0x8012 -- T8012 = T2 Security Chip (APL1027), Mac T2 SoC
     * (iBridge2, based on A10 Fusion).
     * SRTG:[iBoot-3401.0.0.1.16]. gaster.c:784 */
    {
        .cpid = 0x8012,
        .name = "T2",
        .marketing = "Apple T2 Security Chip",
        .checkm8_compatible = 1,
        .config_overwrite_pad = 0x540,
        .config_hole          = 6,
        .insecure_memory_base = 0x18001C000,
        .patch_addr           = 0x100004854,
        .memcpy_addr          = 0x10000EA30,
        .aes_crypto_cmd       = 0x1000082AC,
        .boot_tramp_end       = 0x18001C000,
        .gUSBSerialNumber     = 0x180003AF8,
        .dfu_handle_request   = 0x180008B08,
        .usb_core_do_transfer = 0x10000BD20,
        .dfu_handle_bus_reset = 0x180008B38,
        .handle_interface_request          = 0x10000BFFC,
        .usb_create_string_descriptor      = 0x10000B1CC,
        .usb_serial_number_string_descriptor = 0x18000082A,
        .ttbr0_addr           = 0x18000C000,
        .tlbi                 = 0x100000494,
        .nop_addr           = 0x100008DB8,
        .ret_addr           = 0x10000012C,
        .func_addr          = 0x100008DA0,
        .write_ttbr0          = 0x100000444,
        .ttbr0_vrom_off       = 0x400,
        .ttbr0_sram_off       = 0x600,
    },

    /* ================================================================ */
    /* A12 and later: NOT checkm8-compatible. All offsets zeroed.        */
    /* Marketing/name entries only exist so runtime identification and   */
    /* --print-chip-db can honestly report "unsupported chip" rather     */
    /* than "unknown CPID". Not present in gaster (out of scope: T12).   */
    /* ================================================================ */

    { .cpid = 0x8020, .name = "A12",  .marketing = "iPhone XS / iPhone XR"             },
    { .cpid = 0x8027, .name = "A12Z", .marketing = "iPad Pro (4th gen)"                },
    { .cpid = 0x8030, .name = "A13",  .marketing = "iPhone 11 / iPhone 11 Pro"         },
    { .cpid = 0x8101, .name = "A14",  .marketing = "iPhone 12 / iPad Air (4th gen)"    },
    { .cpid = 0x8103, .name = "M1",   .marketing = "iPad Pro (5th gen) / MacBook (M1)" },
    { .cpid = 0x8110, .name = "A15",  .marketing = "iPhone 13 / iPhone SE (3rd gen)"   },
    { .cpid = 0x8112, .name = "M2",   .marketing = "iPad Pro (6th gen) / MacBook (M2)" },
    { .cpid = 0x8120, .name = "A16",  .marketing = "iPhone 14 Pro / iPhone 14 Pro Max" },
    { .cpid = 0x8130, .name = "A17P", .marketing = "iPhone 15 Pro / iPhone 15 Pro Max" },

    /* Null-terminated sentinel */
    { 0 }
};
/* clang-format on */

#endif /* CHIP_DB_TABLE_EXT_H */
