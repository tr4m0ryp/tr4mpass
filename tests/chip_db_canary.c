/*
 * chip_db_canary.c -- build-time integrity probe for the chip database.
 *
 * Compiled and run by the Makefile's .chip-db-canary target, which
 * is a prerequisite of the main tr4mpass binary when
 * STRICT_CHIP_DB=1 (default). If any well-known row diverges from
 * the expected (name substring, marketing substring), the probe
 * exits non-zero and the build fails.
 *
 * The expected substrings are intentionally short and stable so a
 * legitimate marketing rewrite (e.g. spelling change, added SKU)
 * does not spuriously break the build. The intent is to catch a
 * repeat of issue #53 -- where 0x8947 was labeled "A7" and 0x8960
 * "A10" -- not to freeze every character of every string.
 *
 * If a distro-packaged chip table has legitimately diverged from
 * this file's expectations, `make STRICT_CHIP_DB=0` pipelinees the
 * probe entirely (T5 "Chip-DB canary strictness" note).
 *
 * All rows must remain traceable to gaster.c line numbers -- see
 * include/device/chip_db_table.h and chip_db_table_ext.h.
 */

#include "device/chip_db.h"
#include "device/chip_db_table.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct expected_row {
    uint32_t    cpid;
    int         checkm8_compatible;
    const char *name_substr;
    const char *marketing_substr;
};

static const struct expected_row g_expected[] = {
    /* A5 Rev B -- historically mislabeled as "A7 / iPhone 5s" (issue #53). */
    { 0x8947, 1, "S5L8947", "A5 Rev B" },
    /* A6 / A6X -- historically mislabeled as "Apple TV (A5-class)". */
    { 0x8950, 1, "A6",      "iPhone 5" },
    { 0x8955, 1, "A6X",     "iPad" },
    /* Watch S-series and T1. 0x8002 was mislabeled "S3 Series 3". */
    { 0x7002, 1, "S1P",     "Watch Series 1" },
    { 0x8002, 1, "S1P",     "T1" },
    { 0x8004, 1, "S3",      "Watch Series 3" },
    /* A7 / A8 / A8X -- 0x8960 was mislabeled as "A10 / iPhone 7" (issue #53). */
    { 0x8960, 1, "A7",      "iPhone 5s" },
    { 0x7000, 1, "A8",      "iPod touch 6G" },
    { 0x7001, 1, "A8X",     "iPad Air 2" },
    /* A9 foundry variants -- Samsung/TSMC were historically swapped. */
    { 0x8000, 1, "A9",      "Samsung" },
    { 0x8003, 1, "A9",      "TSMC" },
    /* A9X / A10 / A10X / A11 / T2. */
    { 0x8001, 1, "A9X",     "iPad Pro" },
    { 0x8010, 1, "A10",     "iPhone 7" },
    { 0x8011, 1, "A10X",    "iPad Pro" },
    { 0x8015, 1, "A11",     "iPhone" },
    { 0x8012, 1, "T2",      "T2" },
    /* A12+ must be present but explicitly not checkm8-compatible. */
    { 0x8020, 0, "A12",     "iPhone" },
    { 0x8101, 0, "A14",     "iPhone" },
};

#ifndef STRICT_CHIP_DB
#define STRICT_CHIP_DB 1
#endif

int main(void)
{
#if STRICT_CHIP_DB
    size_t failed = 0;
    size_t i;
    const size_t n = sizeof(g_expected) / sizeof(g_expected[0]);

    for (i = 0; i < n; i++) {
        const struct expected_row *exp = &g_expected[i];
        const chip_info_t *row = NULL;
        const chip_info_t *p;

        for (p = g_chip_table; p->name != NULL; p++) {
            if (p->cpid == exp->cpid) {
                row = p;
                break;
            }
        }
        if (row == NULL) {
            fprintf(stderr,
                    "[chip-db-canary] FAIL: CPID 0x%04X missing from g_chip_table\n",
                    exp->cpid);
            failed++;
            continue;
        }
        if (row->checkm8_compatible != exp->checkm8_compatible) {
            fprintf(stderr,
                    "[chip-db-canary] FAIL: CPID 0x%04X checkm8_compatible=%d expected %d\n",
                    exp->cpid, row->checkm8_compatible, exp->checkm8_compatible);
            failed++;
        }
        if (strstr(row->name, exp->name_substr) == NULL) {
            fprintf(stderr,
                    "[chip-db-canary] FAIL: CPID 0x%04X .name=\"%s\" missing \"%s\"\n",
                    exp->cpid, row->name, exp->name_substr);
            failed++;
        }
        if (strstr(row->marketing, exp->marketing_substr) == NULL) {
            fprintf(stderr,
                    "[chip-db-canary] FAIL: CPID 0x%04X .marketing=\"%s\" missing \"%s\"\n",
                    exp->cpid, row->marketing, exp->marketing_substr);
            failed++;
        }
    }

    if (failed != 0) {
        fprintf(stderr,
                "[chip-db-canary] FAIL: %zu divergence(s). Rebuild chip_db_table*.h\n"
                "                 from gaster.c:519-808 (0x7ff/gaster) or\n"
                "                 pipeline with `make STRICT_CHIP_DB=0`.\n",
                failed);
        return 1;
    }

    fprintf(stderr, "[chip-db-canary] OK: %zu rows verified\n", n);
    return 0;
#else
    (void)g_expected;
    (void)g_chip_table;
    fprintf(stderr, "[chip-db-canary] SKIPPED (STRICT_CHIP_DB=0)\n");
    return 0;
#endif
}
