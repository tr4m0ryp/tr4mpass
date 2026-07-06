/*
 * mock_section_stubs.c -- No-op section entry points for `make test`.
 *
 * The hw-independent test target (`make test`) does not link
 * tests/mocks/ and does not pull in src/bypass/path_a_*.c, but it
 * does invoke run_ramdisk_tests() / run_ssh_jailbreak_tests() /
 * run_path_a_integration_tests() / run_path_b_integration_tests()
 * from test_main.c.  These stubs satisfy the linker without running
 * any hardware-dependent logic; the real bodies live in
 * test_ramdisk.c, test_ssh_jailbreak.c, and
 * tests/integration/test_path_{a,b}_e2e.c and ship with
 * `make test-mocks`.
 */

#include <stdio.h>

void run_ramdisk_tests(void)
{
    printf("--- Section 7: Ramdisk delivery (skipped, run via test-mocks) ---\n");
}

void run_ssh_jailbreak_tests(void)
{
    printf("--- Section 8: SSH jailbreak (skipped, run via test-mocks) ---\n");
}

void run_path_a_integration_tests(void)
{
    printf("--- Section 10: Path A E2E (skipped, run via test-mocks) ---\n");
}

void run_path_b_integration_tests(void)
{
    printf("--- Section 11: Path B E2E (skipped, run via test-mocks) ---\n");
}

void run_path_b_bootimg_tests(void)
{
    printf("--- Section 12: Path B bootimg (skipped, run via test-mocks) ---\n");
}
