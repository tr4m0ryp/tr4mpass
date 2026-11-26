/*
 * test_cli_options.c -- Main CLI policy tests.
 *
 * Including the production translation unit exposes its static parsing and
 * selection helpers without introducing a second main() into the mock
 * suite.  The mock target excludes src/main.c, so these definitions do
 * not collide with production code.
 */

#include "test_framework.h"

#define main tr4mpass_cli_test_program_main
#include "../../src/main.c"
#undef main

#include <string.h>

static void test_experimental_kis_option_is_explicit(void)
{
    cli_opts_t opts;
    char program[] = "tr4mpass";
    char option[] = "--experimental-kis-transition";
    char *argv[] = { program, option, NULL };

    printf("  [cli] parses the explicit KIS transition option\n");

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(2, argv, &opts), 0);
    ASSERT_EQ(opts.experimental_kis_transition, 1);
}

static void test_forced_path_a_rejects_kis(void)
{
    device_info_t dev;
    cli_opts_t opts;

    printf("  [cli] rejects forced Path A for KIS\n");

    memset(&dev, 0, sizeof(dev));
    dev.transport = DEVICE_TRANSPORT_KIS_DFU;
    dev.is_dfu_mode = 1;
    dev.checkm8_vulnerable = 1;

    memset(&opts, 0, sizeof(opts));
    opts.force_path_a = 1;
    ASSERT_NULL(select_module(&dev, &opts));

    dev.transport = DEVICE_TRANSPORT_CLASSIC_DFU;
    ASSERT_NOTNULL(select_module(&dev, &opts));
}

void run_cli_options_tests(void)
{
    test_experimental_kis_option_is_explicit();
    test_forced_path_a_rejects_kis();
}
