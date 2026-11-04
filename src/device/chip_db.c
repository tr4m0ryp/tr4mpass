/* chip_db.c -- chip database lookup API */

#include "device/chip_db.h"

#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>

/* The static table lives in a separate header to respect the 300-line limit */
#include "device/chip_db_table.h"

const chip_info_t *
chip_db_lookup(uint32_t cpid)
{
    const chip_info_t *p;

    for (p = g_chip_table; p->name != NULL; p++) {
        if (p->cpid == cpid)
            return p;
    }
    return NULL;
}

const char *
chip_db_name(uint32_t cpid)
{
    const chip_info_t *info = chip_db_lookup(cpid);
    return (info != NULL) ? info->name : "Unknown";
}

int
chip_db_is_checkm8_compatible(uint32_t cpid)
{
    const chip_info_t *info = chip_db_lookup(cpid);
    return (info != NULL) ? info->checkm8_compatible : 0;
}

void
chip_db_print_all(void)
{
    const chip_info_t *p;

    printf("%-8s %-5s %-38s %4s %5s %5s %4s %18s\n",
           "CPID", "Name", "Marketing", "Compat", "Leak", "OvPad", "Hole",
           "InsecureMemBase");
    printf("%-8s %-5s %-38s %4s %5s %5s %4s %18s\n",
           "--------", "-----", "--------------------------------------",
           "----", "-----", "-----", "----", "------------------");

    for (p = g_chip_table; p->name != NULL; p++) {
        printf("0x%04X   %-5s %-38s %4d %5u 0x%03X %4u 0x%09" PRIX64 "\n",
               p->cpid,
               p->name,
               p->marketing,
               p->checkm8_compatible,
               (unsigned)p->config_large_leak,
               (unsigned)p->config_overwrite_pad,
               (unsigned)p->config_hole,
               p->insecure_memory_base);
    }
}
