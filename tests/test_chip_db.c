/*
 * test_chip_db.c -- Chip database lookup tests.
 *
 * Exercises chip_db_lookup against the static CPID table built into
 * src/device/chip_db.c.
 */

#include "test_framework.h"

#include "device/chip_db.h"

#include <string.h>

static void test_chip_db_a9_samsung(void)
{
    const chip_info_t *c = chip_db_lookup(0x8000);
    ASSERT_NOTNULL(c);
    ASSERT_EQ(c->checkm8_vulnerable, 1);
    ASSERT(strstr(c->name, "A9") != NULL);
}

static void test_chip_db_a9_tsmc(void)
{
    const chip_info_t *c = chip_db_lookup(0x8003);
    ASSERT_NOTNULL(c);
    ASSERT_EQ(c->checkm8_vulnerable, 1);
}

static void test_chip_db_a11(void)
{
    const chip_info_t *c = chip_db_lookup(0x8015);
    ASSERT_NOTNULL(c);
    ASSERT_EQ(c->checkm8_vulnerable, 1);
    ASSERT(strstr(c->name, "A11") != NULL);
}

static void test_chip_db_a12(void)
{
    const chip_info_t *c = chip_db_lookup(0x8020);
    ASSERT_NOTNULL(c);
    ASSERT_EQ(c->checkm8_vulnerable, 0);
}

static void test_chip_db_a14(void)
{
    const chip_info_t *c = chip_db_lookup(0x8101);
    ASSERT_NOTNULL(c);
    ASSERT_EQ(c->checkm8_vulnerable, 0);
}

static void test_chip_db_unknown(void)
{
    ASSERT_NULL(chip_db_lookup(0xDEAD));
}

static void test_chip_db_zero(void)
{
    ASSERT_NULL(chip_db_lookup(0x0000));
}

void run_chip_db_tests(void)
{
    printf("--- Section 2: Chip database ---\n");
    test_chip_db_a9_tsmc();
    test_chip_db_a9_samsung();
    test_chip_db_a11();
    test_chip_db_a12();
    test_chip_db_a14();
    test_chip_db_unknown();
    test_chip_db_zero();
}
