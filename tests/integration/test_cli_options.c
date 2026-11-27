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

#include "mocks/mock_control.h"

#include <stdint.h>
#include <string.h>

#define RESUME_TEST_ECID UINT64_C(0x000A7DE22260201E)

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

static void test_resume_option_is_explicit_long_only(void)
{
    cli_opts_t opts;
    char program[] = "tr4mpass";
    char option[] = "--resume-recovery";
    char ecid_arg[] = "--ecid";
    char ecid[] = "0x000A7DE22260201E";
    char *argv[] = { program, option, ecid_arg, ecid, NULL };

    printf("  [cli] parses the explicit Recovery resume option\n");

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(4, argv, &opts), 0);
    ASSERT_EQ(opts.resume_recovery, 1);
    ASSERT_EQ(opts.has_ecid_override, 1);
    ASSERT_EQ(opts.ecid_override, RESUME_TEST_ECID);

    {
        char short_option[] = "-r";
        char *short_argv[] = { program, short_option, NULL };

        memset(&opts, 0, sizeof(opts));
        optind = 1;
        ASSERT_EQ(parse_args(2, short_argv, &opts), 0);
        ASSERT_EQ(opts.resume_recovery, 0);
    }
}

static void test_resume_requires_exact_ecid(void)
{
    cli_opts_t opts;
    char program[] = "tr4mpass";
    char option[] = "--resume-recovery";
    char *missing_ecid[] = { program, option, NULL };
    char ecid_arg[] = "--ecid";
    char zero_ecid[] = "0";
    char *zero_argv[] = { program, option, ecid_arg, zero_ecid, NULL };

    printf("  [cli] Recovery resume requires an explicit exact ECID\n");

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(2, missing_ecid, &opts), -1);

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(4, zero_argv, &opts), -1);
}

static void test_resume_rejects_conflicting_modes(void)
{
    cli_opts_t opts;
    char program[] = "tr4mpass";
    char option[] = "--resume-recovery";
    char ecid_arg[] = "--ecid";
    char ecid[] = "0x000A7DE22260201E";
    char path_a[] = "--force-path-a";
    char kis[] = "--experimental-kis-transition";
    char *path_a_argv[] = { program, option, ecid_arg, ecid, path_a, NULL };
    char *kis_argv[] = { program, option, ecid_arg, ecid, kis, NULL };

    printf("  [cli] Recovery resume rejects conflicting modes\n");

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(5, path_a_argv, &opts), -1);

    memset(&opts, 0, sizeof(opts));
    optind = 1;
    ASSERT_EQ(parse_args(5, kis_argv, &opts), -1);
}

static void test_resume_detection_precedes_dfu_probe(void)
{
    device_info_t dev;
    cli_opts_t opts;
    const char *serial = "CPID:8120 ECID:000A7DE22260201E";

    printf("  [cli] Recovery resume probes Recovery before DFU\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    memset(&opts, 0, sizeof(opts));
    opts.resume_recovery = 1;
    opts.has_ecid_override = 1;
    opts.ecid_override = RESUME_TEST_ECID;
    mock_irecv_set_device_identity(0x1281, 0x1281, 0x8120,
                                   RESUME_TEST_ECID, serial);

    ASSERT_EQ(detect_device(&dev, &opts), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(dev.boot_mode, 0x1281);
    ASSERT_EQ(dev.ecid, RESUME_TEST_ECID);
    ASSERT_EQ(mock_call_log_contains("usb_dfu_find"), 0);
    ASSERT_EQ(mock_call_log_contains("libusb_control_transfer"), 0);
}

void run_cli_options_tests(void)
{
    test_experimental_kis_option_is_explicit();
    test_forced_path_a_rejects_kis();
    test_resume_option_is_explicit_long_only();
    test_resume_requires_exact_ecid();
    test_resume_rejects_conflicting_modes();
    test_resume_detection_precedes_dfu_probe();
}
