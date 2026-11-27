/*
 * test_kis_transport.c -- Mock-backed KIS identity detection tests.
 */

#include "test_framework.h"

#include "bypass/path_a.h"
#include "bypass/path_b.h"
#include "bypass/path_b_transition.h"
#include "device/device.h"
#include "device/kis_dfu.h"
#include "mocks/mock_control.h"

#include <stdint.h>
#include <string.h>

void run_cli_options_tests(void);

#define KIS_TEST_CPID 0x8120u
#define KIS_TEST_ECID UINT64_C(0x000A7DE22260201E)

static void test_kis_detection_copies_identity(void)
{
    device_info_t dev;
    const char *serial = "CPID:8120 CPRV:11 ECID:000A7DE22260201E";

    printf("  [kis] detects KIS-carried DFU identity\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, serial);

    ASSERT_EQ(kis_dfu_detect(&dev), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_KIS_DFU);
    ASSERT_EQ(dev.usb_product_id, 0x1881);
    ASSERT_EQ(dev.boot_mode, 0x1227);
    ASSERT_EQ(dev.cpid, KIS_TEST_CPID);
    ASSERT_EQ(dev.ecid, KIS_TEST_ECID);
    ASSERT_STREQ(dev.serial, serial);
    ASSERT_EQ(dev.is_dfu_mode, 1);
    ASSERT_NULL(dev.usb);
    ASSERT_EQ(mock_call_log_contains("libusb_control_transfer"), 0);
}

static void test_recovery_is_not_kis(void)
{
    device_info_t dev;

    printf("  [kis] rejects Recovery identity\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1281, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "recovery serial");

    ASSERT_EQ(kis_dfu_detect(&dev), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1881, 0x1281, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "recovery serial");

    ASSERT_EQ(kis_dfu_detect(&dev), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
}

static void test_kis_open_failure_leaves_identity_unset(void)
{
    device_info_t dev;

    printf("  [kis] open failure leaves identity unset\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_queue_open_error(-1);

    ASSERT_EQ(kis_dfu_detect(&dev), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);
    ASSERT_EQ(dev.is_dfu_mode, 0);
    ASSERT_NULL(dev.usb);
}

static int count_log_entries(const char *needle)
{
    size_t i;
    int count = 0;

    for (i = 0; i < mock_get_call_count(); i++) {
        const char *line = mock_get_call_log(i);
        if (line && strstr(line, needle))
            count++;
    }
    return count;
}

static void test_kis_reboot_requires_exact_ecid(void)
{
    device_info_t dev;

    printf("  [kis] reboot rejects a missing ECID before opening a device\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.transport = DEVICE_TRANSPORT_KIS_DFU;
    dev.usb_product_id = 0x1881;
    dev.boot_mode = 0x1227;

    ASSERT_EQ(kis_dfu_request_reboot(&dev), -1);
    ASSERT_EQ(count_log_entries("irecv_open_with_ecid"), 0);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 0);
}

static void test_kis_reboot_is_one_shot_and_transport_guarded(void)
{
    device_info_t dev;

    printf("  [kis] reboot is one-shot and transport-guarded\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.transport = DEVICE_TRANSPORT_KIS_DFU;
    dev.usb_product_id = 0x1881;
    dev.boot_mode = 0x1227;
    dev.ecid = KIS_TEST_ECID;
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");

    ASSERT_EQ(kis_dfu_request_reboot(&dev), 0);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 1);
    ASSERT_EQ(count_log_entries("libusb_control_transfer"), 0);

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.transport = DEVICE_TRANSPORT_KIS_DFU;
    dev.usb_product_id = 0x1881;
    dev.boot_mode = 0x1227;
    dev.ecid = KIS_TEST_ECID;
    mock_irecv_set_device_identity(0x1281, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "recovery serial");

    ASSERT_EQ(kis_dfu_request_reboot(&dev), -1);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 0);
}

static void test_kis_path_selection(void)
{
    device_info_t dev;

    printf("  [kis] selects Path B and never raw-DFU Path A\n");

    memset(&dev, 0, sizeof(dev));
    dev.is_dfu_mode = 1;
    dev.transport = DEVICE_TRANSPORT_KIS_DFU;
    dev.usb_product_id = 0x1881;
    dev.boot_mode = 0x1227;
    dev.checkm8_vulnerable = 0;

    ASSERT_EQ(path_a_module.probe(&dev), 0);
    ASSERT_EQ(path_b_module.probe(&dev), 1);

    dev.checkm8_vulnerable = 1;
    ASSERT_EQ(path_a_module.probe(&dev), 0);
    ASSERT_EQ(path_b_module.probe(&dev), 0);

    ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_KIS_DFU), 0);

    dev.transport = DEVICE_TRANSPORT_CLASSIC_DFU;
    dev.usb_product_id = 0x1227;
    ASSERT_EQ(path_a_module.probe(&dev), 1);
}

static libusb_device_handle *fake_usb_handle(void)
{
    static int sentinel;
    return (libusb_device_handle *)&sentinel;
}

static void init_kis_transition_device(device_info_t *dev, uint64_t ecid)
{
    memset(dev, 0, sizeof(*dev));
    dev->transport = DEVICE_TRANSPORT_KIS_DFU;
    dev->usb_product_id = 0x1881;
    dev->boot_mode = 0x1227;
    dev->is_dfu_mode = 1;
    dev->cpid = KIS_TEST_CPID;
    dev->ecid = ecid;
    dev->usb = NULL;
}

static void test_kis_transition_requires_opt_in_and_ecid(void)
{
    device_info_t dev;

    printf("  [kis] transition requires opt-in and exact ECID\n");

    mock_reset_all();
    init_kis_transition_device(&dev, KIS_TEST_ECID);
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), -1);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 0);

    mock_reset_all();
    init_kis_transition_device(&dev, 0);
    dev.experimental_kis_transition = 1;
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), -1);
    ASSERT_EQ(count_log_entries("irecv_open_with_ecid"), 0);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 0);
}

static void test_kis_transition_reaches_recovery(void)
{
    device_info_t dev;
    int modes[] = { 0x1227, 0x1281 };

    printf("  [kis] transition accepts only confirmed Recovery mode\n");

    mock_reset_all();
    init_kis_transition_device(&dev, KIS_TEST_ECID);
    dev.experimental_kis_transition = 1;
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");
    mock_irecv_set_mode_sequence(modes, sizeof(modes) / sizeof(modes[0]));

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), 0);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 1);
    ASSERT_EQ(count_log_entries("libusb_control_transfer"), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(dev.boot_mode, 0x1281);
    ASSERT_EQ(dev.usb_product_id, 0x1281);
    ASSERT_EQ(dev.is_dfu_mode, 0);
}

static void test_kis_transition_waits_after_reboot_error(void)
{
    device_info_t dev;
    int modes[] = { 0x1227, 0x1281 };

    printf("  [kis] transition waits for Recovery after a reboot error\n");

    mock_reset_all();
    init_kis_transition_device(&dev, KIS_TEST_ECID);
    dev.experimental_kis_transition = 1;
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");
    mock_irecv_set_reboot_error(-3);
    mock_irecv_set_mode_sequence(modes, sizeof(modes) / sizeof(modes[0]));

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), 0);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(dev.boot_mode, 0x1281);
}

static void test_kis_transition_times_out_without_false_recovery(void)
{
    device_info_t dev;
    int modes[] = { 0x1227 };

    printf("  [kis] transition times out without false Recovery success\n");

    mock_reset_all();
    init_kis_transition_device(&dev, KIS_TEST_ECID);
    dev.experimental_kis_transition = 1;
    mock_irecv_set_device_identity(0x1881, 0x1227, KIS_TEST_CPID,
                                   KIS_TEST_ECID, "CPID:8120 ECID:000A7DE22260201E");
    mock_irecv_set_mode_sequence(modes, sizeof(modes) / sizeof(modes[0]));

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_KIS_DFU);
    ASSERT_EQ(dev.boot_mode, 0x1227);
}

static void test_classic_transition_uses_only_dfu_abort(void)
{
    device_info_t dev;
    int modes[] = { 0x1281 };

    printf("  [classic] transition uses DFU abort, never KIS reboot\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.transport = DEVICE_TRANSPORT_CLASSIC_DFU;
    dev.usb_product_id = 0x1227;
    dev.boot_mode = 0x1227;
    dev.is_dfu_mode = 1;
    dev.cpid = 0x8015;
    dev.ecid = KIS_TEST_ECID;
    dev.usb = fake_usb_handle();
    mock_irecv_set_device_identity(0x1281, 0x1281, 0x8015,
                                   KIS_TEST_ECID, "CPID:8015 ECID:000A7DE22260201E");
    mock_irecv_set_mode_sequence(modes, sizeof(modes) / sizeof(modes[0]));

    ASSERT_EQ(path_b_transition_to_recovery(&dev, 3, 0), 0);
    ASSERT_EQ(count_log_entries("libusb_control_transfer"), 1);
    ASSERT_EQ(count_log_entries("irecv_reboot"), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(dev.boot_mode, 0x1281);
    ASSERT_NULL(dev.usb);
}

void run_kis_transport_tests(void)
{
    printf("--- Section 13: KIS transport identity ---\n");
    test_kis_detection_copies_identity();
    test_recovery_is_not_kis();
    test_kis_open_failure_leaves_identity_unset();
    test_kis_reboot_requires_exact_ecid();
    test_kis_reboot_is_one_shot_and_transport_guarded();
    test_kis_path_selection();
    test_kis_transition_requires_opt_in_and_ecid();
    test_kis_transition_reaches_recovery();
    test_kis_transition_waits_after_reboot_error();
    test_kis_transition_times_out_without_false_recovery();
    test_classic_transition_uses_only_dfu_abort();
    run_cli_options_tests();
}
