/*
 * test_constants.c -- Compile-time retry / delay constants.
 */

#include "test_framework.h"

#include "exploit/checkm8_internal.h"

static void test_retry_constant(void)
{
    ASSERT_EQ(MAX_EXPLOIT_TRIES, 3);
}

static void test_reconnect_delay(void)
{
    ASSERT_EQ(USB_RECONNECT_DELAY_USEC, 8000000);
}

void run_constant_tests(void)
{
    printf("--- Section 4: Exploit constants ---\n");
    test_retry_constant();
    test_reconnect_delay();
}
