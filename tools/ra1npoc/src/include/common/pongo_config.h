#ifndef PONGO_CONFIG_H
#define PONGO_CONFIG_H

/*
 * pongo_config - pongoOS config
 *
 */

#define BOOTARGS_STR_PTR    (0x15801E)
#define KPF_FLAGS_PTR       (0x158018)
#define KPF_LOCATION        (0x3F308)
#define KPF_SIZE            (0x15648)
#define RDSK_SIZE           (0x100000)
#define RDSK_LOCATION       (0x58000)
#define BLANK_SIZE          (0x154C) // after ramdisk, buffer storing bootargs and flags..?
// no idea :/
#define SPECIAL_HAXX        (0x3000)
#define MAX_HAXX_SIZE       (0x200000)

/*
 * Copyright (C) 2019-2021 checkra1n team
 * This file is part of pongoOS.
 */

#define DEFAULT_BOOTARGS "rootdev=md0"
#define MAX_BOOTARGS_LEN 256
typedef enum {
    checkrain_option_none               = 0,
    checkrain_option_all                = -1,
    checkrain_option_failure            = -2,
    
    checkrain_option_safemode           = 1 << 0,
    checkrain_option_verbose_boot       = 1 << 1,
    checkrain_option_verbose_logging    = 1 << 2,
    checkrain_option_demote             = 1 << 3,
    checkrain_option_pongo_shell        = 1 << 4,
    checkrain_option_early_exit         = 1 << 5,
} checkrain_option_t, *checkrain_option_p;

#define checkrain_set_option(options, option, enabled) do { \
if (enabled)                                            \
options = (checkrain_option_t)(options | option);   \
else                                                    \
options = (checkrain_option_t)(options & ~option);  \
} while (0);

#endif
