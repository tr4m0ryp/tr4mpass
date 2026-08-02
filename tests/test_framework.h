/*
 * test_framework.h -- Assertion macros and counters for tr4mpass unit tests.
 *
 * Shared by every test_*.c file.  The counters live in test_main.c.
 * Include this header from each test translation unit; do not duplicate
 * the `extern int g_passes` in the .c files.
 */

#ifndef TR4MPASS_TEST_FRAMEWORK_H
#define TR4MPASS_TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

/* Pass/fail counters defined in tests/test_main.c. */
extern int g_passes;
extern int g_failures;

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } else { \
        g_passes++; \
    } \
} while (0)

#define ASSERT_EQ(a, b)     ASSERT((a) == (b))
#define ASSERT_NEQ(a, b)    ASSERT((a) != (b))
#define ASSERT_NULL(p)      ASSERT((p) == NULL)
#define ASSERT_NOTNULL(p)   ASSERT((p) != NULL)
/* ASSERT_STREQ / ASSERT_STRSTR: do not null-guard the second argument —
 * it is always a string literal or stack array (always non-null), and
 * the check triggers -Waddress on GCC.  Keep the first-arg guard for
 * ASSERT_MEM_EQ since the first argument (heap pointer) may be NULL. */
#define ASSERT_STREQ(a, b)  ASSERT(strcmp((a), (b)) == 0)
#define ASSERT_STRSTR(h, n) ASSERT(strstr((h), (n)) != NULL)
#define ASSERT_MEM_EQ(a, b, n) ASSERT((a) && memcmp((a), (b), (n)) == 0)
#define ASSERT_LT(a, b)     ASSERT((a) < (b))
#define ASSERT_GT(a, b)     ASSERT((a) > (b))

/* ------------------------------------------------------------------ */
/* Section entry points                                               */
/* ------------------------------------------------------------------ */

/* Existing hardware-independent tests */
void run_serial_parse_tests(void);
void run_chip_db_tests(void);
void run_bypass_probe_tests(void);
void run_constant_tests(void);

/* Phase 2 placeholders (filled in by other agents) */
void run_cert_extract_tests(void);
void run_signer_tests(void);
void run_ramdisk_tests(void);
void run_ssh_jailbreak_tests(void);

/* Phase 3 integration sections (mock-only) */
void run_path_a_integration_tests(void);
void run_path_b_integration_tests(void);

#endif /* TR4MPASS_TEST_FRAMEWORK_H */
