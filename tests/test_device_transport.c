/*
 * test_device_transport.c -- USB transport classification behavior.
 */

#include "test_framework.h"

#include "device/device_transport.h"

static void test_transport_from_usb_pid(void)
{
    ASSERT_EQ(device_transport_from_usb_pid(0x1227),
              DEVICE_TRANSPORT_CLASSIC_DFU);
    ASSERT_EQ(device_transport_from_usb_pid(0x1881),
              DEVICE_TRANSPORT_KIS_DFU);
    ASSERT_EQ(device_transport_from_usb_pid(0x1281),
              DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(device_transport_from_usb_pid(0x1283),
              DEVICE_TRANSPORT_RECOVERY);
    ASSERT_EQ(device_transport_from_usb_pid(0x1234),
              DEVICE_TRANSPORT_NORMAL);
}

static void test_recovery_mode_classification(void)
{
    ASSERT_EQ(device_transport_is_recovery_mode(0x1280), 1);
    ASSERT_EQ(device_transport_is_recovery_mode(0x1283), 1);
    ASSERT_EQ(device_transport_is_recovery_mode(0x1227), 0);
    ASSERT_EQ(device_transport_is_recovery_mode(0x1881), 0);
}

static void test_path_a_transport_policy(void)
{
    ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_CLASSIC_DFU), 1);
    ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_KIS_DFU), 0);
    ASSERT_EQ(device_transport_allows_path_a(DEVICE_TRANSPORT_RECOVERY), 0);
}

static void test_transport_name(void)
{
    ASSERT_STREQ(device_transport_name(DEVICE_TRANSPORT_NORMAL), "Normal");
    ASSERT_STREQ(device_transport_name(DEVICE_TRANSPORT_CLASSIC_DFU),
                  "Classic DFU");
    ASSERT_STREQ(device_transport_name(DEVICE_TRANSPORT_KIS_DFU),
                  "KIS DFU (Debug USB)");
    ASSERT_STREQ(device_transport_name(DEVICE_TRANSPORT_RECOVERY),
                  "Recovery");
}

void run_device_transport_tests(void)
{
    printf("--- Section 12: USB transport model ---\n");
    test_transport_from_usb_pid();
    test_recovery_mode_classification();
    test_path_a_transport_policy();
    test_transport_name();
}
