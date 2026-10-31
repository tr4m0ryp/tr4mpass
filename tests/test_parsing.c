/*
 * test_parsing.c -- DFU serial CPID/ECID parsing tests.
 *
 * Inline copy of parse_hex_field logic from src/device/usb_dfu.c, tested
 * independently of the hardware-dependent USB code.
 */

#include "test_framework.h"

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int test_parse_hex_field(const char *serial, const char *key,
                                uint64_t *out)
{
    const char *p;
    char *endptr;
    const char *start;

    if (!serial || !key || !out)
        return -1;
    p = strstr(serial, key);
    if (!p)
        return -1;
    start = p + strlen(key);
    errno = 0;
    endptr = NULL;
    *out = strtoull(start, &endptr, 16);
    if (errno != 0 || endptr == start ||
        (*endptr != '\0' && *endptr != ' ' && *endptr != '\t' &&
         *endptr != '\r' && *endptr != '\n')) {
        *out = 0;
        return -1;
    }
    return 0;
}

static void test_parse_cpid_normal(void)
{
    const char *serial = "CPID:8015 CPRV:11 BDID:0C ECID:001A2B3C4D5E6F70 "
                         "IBFL:3C SRTG:[iBoot-4142.0.0.0.1]";
    uint64_t cpid = 0, ecid = 0;

    ASSERT_EQ(test_parse_hex_field(serial, "CPID:", &cpid), 0);
    ASSERT_EQ(cpid, (uint64_t)0x8015);

    ASSERT_EQ(test_parse_hex_field(serial, "ECID:", &ecid), 0);
    ASSERT_EQ(ecid, (uint64_t)0x001A2B3C4D5E6F70ULL);
}

static void test_parse_cpid_a9_samsung(void)
{
    const char *serial = "CPID:8003 CPRV:10 BDID:02 ECID:AABB112233445566";
    uint64_t cpid = 0;

    ASSERT_EQ(test_parse_hex_field(serial, "CPID:", &cpid), 0);
    ASSERT_EQ(cpid, (uint64_t)0x8003);
}

static void test_parse_cpid_empty_serial(void)
{
    /* Bare DFU string -- no CPID/ECID fields */
    const char *serial = "Apple Mobile Device (DFU Mode)";
    uint64_t cpid = 0xDEAD, ecid = 0xDEAD;

    ASSERT_EQ(test_parse_hex_field(serial, "CPID:", &cpid), -1);
    ASSERT_EQ(test_parse_hex_field(serial, "ECID:", &ecid), -1);
    /* Outputs unchanged on failure */
    ASSERT_EQ(cpid, (uint64_t)0xDEAD);
    ASSERT_EQ(ecid, (uint64_t)0xDEAD);
}

static void test_parse_cpid_partial(void)
{
    /* CPID present, ECID absent */
    const char *serial = "CPID:8960 CPRV:01";
    uint64_t cpid = 0, ecid = 0xBEEF;

    ASSERT_EQ(test_parse_hex_field(serial, "CPID:", &cpid), 0);
    ASSERT_EQ(cpid, (uint64_t)0x8960);
    ASSERT_EQ(test_parse_hex_field(serial, "ECID:", &ecid), -1);
    ASSERT_EQ(ecid, (uint64_t)0xBEEF);  /* unchanged */
}

static void test_parse_malformed_hex_field(void)
{
    uint64_t value = 0xBEEF;

    ASSERT_EQ(test_parse_hex_field("CPID:xyz CPRV:11", "CPID:", &value), -1);
    ASSERT_EQ(value, (uint64_t)0);

    value = 0xBEEF;
    ASSERT_EQ(test_parse_hex_field("CPID:8015junk CPRV:11", "CPID:", &value), -1);
    ASSERT_EQ(value, (uint64_t)0);
}

void run_serial_parse_tests(void)
{
    printf("--- Section 1: DFU serial parsing ---\n");
    test_parse_cpid_normal();
    test_parse_cpid_a9_samsung();
    test_parse_cpid_empty_serial();
    test_parse_cpid_partial();
    test_parse_malformed_hex_field();
}
