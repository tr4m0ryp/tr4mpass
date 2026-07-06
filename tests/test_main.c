/*
 * test_main.c -- Entry point for the tr4mpass unit test suite.
 *
 * Delegates each section to its own file (tests/test_*.c).  The old
 * monolithic tests/test_sections.c has been split into:
 *   - test_parsing.c        -- DFU serial parsing
 *   - test_chip_db.c        -- Chip database
 *   - test_bypass_probe.c   -- Bypass module probe/select
 *   - test_constants.c      -- Compile-time constants
 *   - test_cert_extract.c   -- (Phase 2) cert extraction
 *   - test_signer.c         -- (Phase 2) ECDSA signer
 *   - test_ramdisk.c        -- (Phase 2) ramdisk delivery
 *   - test_ssh_jailbreak.c  -- (Phase 2) SSH jailbreak
 *
 * Build: make test            (hardware-independent subset)
 *        make test-mocks       (full suite with mocked libs)
 * Run:   ./tests/run_tests or ./tests/run_mock_tests
 */

#include "test_framework.h"

#include <stdio.h>

/* Phase 2D: env_config section entry point.  Declared locally so the
 * shared test_framework.h header does not need to be edited. */
void run_env_config_tests(void);

int g_passes   = 0;
int g_failures = 0;

int main(void)
{
    printf("tr4mpass unit tests\n");
    printf("===================\n");

    run_serial_parse_tests();
    run_chip_db_tests();
    run_bypass_probe_tests();
    run_constant_tests();
    run_cert_extract_tests();
    run_signer_tests();
    run_ramdisk_tests();
    run_ssh_jailbreak_tests();
    run_env_config_tests();
    run_path_a_integration_tests();
    run_path_b_integration_tests();
    run_path_b_bootimg_tests();

    printf("===================\n");
    printf("Results: %d passed, %d failed\n", g_passes, g_failures);

    return g_failures > 0 ? 1 : 0;
}
