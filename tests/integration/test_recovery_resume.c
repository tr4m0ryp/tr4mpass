/*
 * test_recovery_resume.c -- Exact-ECID Recovery detection tests.
 */

#include "test_framework.h"

#include "device/device.h"
#include "device/recovery.h"
#include "mocks/mock_control.h"

#include <stdint.h>
#include <string.h>

#define RECOVERY_TEST_CPID 0x8120u
#define RECOVERY_TEST_ECID UINT64_C(0x000A7DE22260201E)

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

static void test_recovery_probe_requires_exact_ecid(void)
{
    device_info_t dev;

    printf("  [recovery] probe requires an exact ECID before opening\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));

    ASSERT_EQ(recovery_detect_exact(&dev, 0), -1);
    ASSERT_EQ(count_log_entries("irecv_open_with_ecid"), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);
    ASSERT_EQ(dev.is_dfu_mode, 0);
    ASSERT_NULL(dev.usb);
}

static void test_recovery_probe_accepts_confirmed_recovery(void)
{
    device_info_t dev;
    const char *serial =
        "CPID:8120 CPRV:11 ECID:000A7DE22260201E IBFL:3D";

    printf("  [recovery] probe accepts confirmed Recovery identity\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1281, 0x1281, RECOVERY_TEST_CPID,
                                   RECOVERY_TEST_ECID, serial);

    ASSERT_EQ(recovery_detect_exact(&dev, RECOVERY_TEST_ECID), 0);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(dev.usb_product_id, 0x1281);
    ASSERT_EQ(dev.boot_mode, 0x1281);
    ASSERT_EQ(dev.cpid, RECOVERY_TEST_CPID);
    ASSERT_EQ(dev.ecid, RECOVERY_TEST_ECID);
    ASSERT_STREQ(dev.serial, serial);
    ASSERT_EQ(dev.is_dfu_mode, 0);
    ASSERT_NULL(dev.usb);
    ASSERT_EQ(count_log_entries("irecv_close"), 1);
}

static void test_recovery_probe_rejects_non_recovery_modes(void)
{
    device_info_t dev;

    printf("  [recovery] probe rejects non-Recovery modes\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1227, 0x1227, RECOVERY_TEST_CPID,
                                   RECOVERY_TEST_ECID, "dfu serial");

    ASSERT_EQ(recovery_detect_exact(&dev, RECOVERY_TEST_ECID), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1881, 0x1881, RECOVERY_TEST_CPID,
                                   RECOVERY_TEST_ECID, "kis serial");

    ASSERT_EQ(recovery_detect_exact(&dev, RECOVERY_TEST_ECID), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);
}

static void test_recovery_probe_rejects_ecid_mismatch(void)
{
    device_info_t dev;
    const uint64_t other_ecid = UINT64_C(0x000A7DE22260201F);

    printf("  [recovery] probe rejects an ECID mismatch before writes\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_set_device_identity(0x1281, 0x1281, RECOVERY_TEST_CPID,
                                   other_ecid, "wrong device serial");

    ASSERT_EQ(recovery_detect_exact(&dev, RECOVERY_TEST_ECID), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);
    ASSERT_EQ(count_log_entries("irecv_setenv"), 0);
    ASSERT_EQ(count_log_entries("irecv_saveenv"), 0);
}

static void test_recovery_probe_open_failure_is_clean(void)
{
    device_info_t dev;

    printf("  [recovery] probe open failure leaves identity unset\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    mock_irecv_queue_open_error(-1);

    ASSERT_EQ(recovery_detect_exact(&dev, RECOVERY_TEST_ECID), -1);
    ASSERT_EQ(dev.transport, DEVICE_TRANSPORT_NORMAL);
    ASSERT_EQ(dev.usb_product_id, 0);
    ASSERT_EQ(dev.boot_mode, 0);
    ASSERT_EQ(dev.cpid, 0);
    ASSERT_EQ(dev.ecid, 0);
}

void run_recovery_resume_tests(void)
{
    printf("--- Section 14: Explicit Recovery resume ---\n");
    test_recovery_probe_requires_exact_ecid();
    test_recovery_probe_accepts_confirmed_recovery();
    test_recovery_probe_rejects_non_recovery_modes();
    test_recovery_probe_rejects_ecid_mismatch();
    test_recovery_probe_open_failure_is_clean();
}
