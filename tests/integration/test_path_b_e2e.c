/*
 * test_path_b_e2e.c -- End-to-end Path B (A12+) pipeline tests.
 *
 * Two boundaries matter for Path B.  First, identity manipulation (DFU
 * read + recovery setenv) is expected to work on real hardware -- iBoot
 * accepts the PWND marker.  Second, activation itself is blocked by the
 * SEP nonce barrier documented in 26.3-vulnerability.md; FairPlayKeyData,
 * RKCData, and SepNonce are sealed by the Secure Enclave.  The tool's
 * honest behaviour on real A12+ hardware is a clean failure at
 * mobileactivation_activate_with_session().
 *
 * These tests therefore assert:
 *   - path_b_manipulate_identity() writes the PWND marker successfully,
 *   - when the SEP barrier rejects activation (simulated via
 *     mock_mobileactivation_set_activate_error), the session layer
 *     surfaces -1 cleanly and get_activation_state still says Unactivated,
 *   - signal_detect_type correctly distinguishes cellular from WiFi-only.
 *
 * We do NOT drive path_b_module.execute() end-to-end: recovery polling
 * and iBSS upload require mocked libusb/libirecovery timing.  The
 * bootimg resolver and per-step helpers are exercised directly.
 */

#include "test_framework.h"
#include "integration_helpers.h"

#include "activation/activation.h"
#include "activation/record.h"
#include "activation/session.h"
#include "bypass/path_b.h"
#include "bypass/signal.h"
#include "device/device.h"
#include "mocks/mock_control.h"

#include <libusb-1.0/libusb.h>
#include <plist/plist.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Error code used to simulate the SEP nonce barrier.  The numeric value
 * is arbitrary; the SUT only checks != MOBILEACTIVATION_E_SUCCESS. */
#define E2E_MOBILEACTIVATION_E_NONCE_INVALID (-7)

/* Build a USB string descriptor: [len, 0x03, UTF-16LE payload...]. */
static size_t
build_string_descriptor(const char *ascii, unsigned char *desc, size_t cap)
{
    size_t slen = strlen(ascii);
    size_t i, pos;
    desc[0] = (unsigned char)(2 + slen * 2);
    desc[1] = 0x03;
    pos = 2;
    for (i = 0; i < slen && pos + 1 < cap; i++) {
        desc[pos++] = (unsigned char)ascii[i];
        desc[pos++] = 0x00;
    }
    return pos;
}

/*
 * Stage two descriptor reads for path_b_manipulate_identity():
 *   1. initial read returns the original serial,
 *   2. verify read returns original + " PWND:[checkm8]" (simulates the
 *      successful setenv having taken effect).
 */
static void
stage_usb_serial_descriptor(const char *ascii_serial)
{
    unsigned char desc[256];
    unsigned char desc_after[256];
    char modified[256];
    size_t n1, n2;
    snprintf(modified, sizeof(modified), "%s PWND:[checkm8]", ascii_serial);
    n1 = build_string_descriptor(ascii_serial, desc,       sizeof(desc));
    n2 = build_string_descriptor(modified,     desc_after, sizeof(desc_after));
    mock_libusb_stage_control_transfer((int)n1, desc,       n1);
    mock_libusb_stage_control_transfer((int)n2, desc_after, n2);
}

/* Static sentinel used as a non-null libusb_device_handle *. */
static libusb_device_handle *
fake_usb_handle(void)
{
    static int sentinel;
    return (libusb_device_handle *)&sentinel;
}

/*
 * Identity manipulation happy path.  Asserts:
 *   - path_b_manipulate_identity returns 0,
 *   - irecv_setenv("serial-number", ...) contained "PWND:[checkm8]",
 *   - irecv_saveenv persisted the change,
 *   - irecv_getenv("serial-number") read back the PWND marker.
 */
static void
test_path_b_e2e_identity_manipulation_succeeds(void)
{
    device_info_t dev;
    const char *current_serial =
        "CPID:8020 CPRV:11 ECID:0022334455667788";
    int rc;
    size_t i;
    int found_pwnd = 0;
    int found_saveenv = 0;
    int found_getenv = 0;

    printf("  [e2e] path_b identity manipulation (happy)\n");

    mock_reset_all();
    dev = e2e_make_device_path_b_cellular();
    dev.usb = fake_usb_handle();
    mock_irecv_set_device_info(0x1281, dev.ecid, current_serial);
    stage_usb_serial_descriptor(current_serial);

    rc = path_b_manipulate_identity(&dev);
    ASSERT_EQ(rc, 0);

    for (i = 0; i < mock_get_call_count(); i++) {
        const char *line = mock_get_call_log(i);
        if (!line) continue;
        if (strstr(line, "irecv_setenv(serial-number") &&
            strstr(line, "PWND:[checkm8]"))
            found_pwnd = 1;
        if (strstr(line, "irecv_saveenv"))
            found_saveenv = 1;
        if (strstr(line, "irecv_getenv(serial-number"))
            found_getenv = 1;
    }
    ASSERT_EQ(found_pwnd,    1);
    ASSERT_EQ(found_saveenv, 1);
    ASSERT_EQ(found_getenv,  1);
}

/*
 * SEP nonce barrier: simulate mobileactivation rejecting the activation
 * record, then drive the activation half of Path B directly and check
 * that session_activate returns -1, the call actually reached the
 * mobileactivation layer, and get_activation_state still says
 * "Unactivated" -- the tool must not mask the barrier.
 */
static void
test_path_b_e2e_activation_rejected_by_nonce_barrier(void)
{
    device_info_t dev;
    plist_t record = NULL;
    plist_t session_info = NULL;
    plist_t handshake = NULL;
    char state[64];
    int rc;

    printf("  [e2e] path_b activation nonce barrier\n");

    mock_reset_all();
    dev = e2e_make_device_path_b_cellular();
    dev.handle   = (idevice_t)(uintptr_t)0x1;
    dev.lockdown = (lockdownd_client_t)(uintptr_t)0x2;

    /* See 26.3-vulnerability.md: SepNonce cannot be produced without
     * SEP-level access, so real hardware rejects the record at this
     * layer.  The test simulates that rejection. */
    mock_mobileactivation_set_activate_error(
        E2E_MOBILEACTIVATION_E_NONCE_INVALID);
    mock_mobileactivation_set_state("Unactivated");

    record = record_build_a12(&dev);
    ASSERT_NOTNULL(record);

    rc = session_get_info(&dev, &session_info);
    ASSERT_EQ(rc, 0);
    ASSERT_NOTNULL(session_info);

    rc = session_drm_handshake(&dev, session_info, &handshake);
    ASSERT_EQ(rc, 0);
    ASSERT_NOTNULL(handshake);

    /* Decisive assertion: with the staged nonce error, activation MUST
     * fail and the failure must NOT be swallowed. */
    rc = session_activate(&dev, record, handshake);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains(
                  "mobileactivation_activate_with_session"), 1);

    rc = activation_get_state(&dev, state, sizeof(state));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(state, "Unactivated");

    plist_free(record);
    plist_free(session_info);
    plist_free(handshake);
}

/*
 * Signal detection: cellular device (IMEI populated) -> SIGNAL_GSM,
 * WiFi-only (no IMEI, no MEID) -> SIGNAL_NONE.
 */
static void
test_path_b_e2e_signal_detection_cellular_vs_wifi(void)
{
    device_info_t dev_cell = e2e_make_device_path_b_cellular();
    device_info_t dev_wifi = e2e_make_device_path_b_wifi();
    char buf[128];
    int rc;

    printf("  [e2e] path_b signal detection (cell vs wifi)\n");

    mock_reset_all();

    ASSERT_EQ(signal_detect_type(&dev_cell), SIGNAL_GSM);
    ASSERT_EQ(signal_can_preserve(&dev_cell), 1);
    rc = signal_get_carrier_id(&dev_cell, buf, sizeof(buf));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(buf, dev_cell.imei);

    ASSERT_EQ(signal_detect_type(&dev_wifi), SIGNAL_NONE);
    ASSERT_EQ(signal_can_preserve(&dev_wifi), 0);
    rc = signal_get_carrier_id(&dev_wifi, buf, sizeof(buf));
    ASSERT_EQ(rc, -1);
}

void
run_path_b_integration_tests(void)
{
    printf("--- Section 11: Path B end-to-end integration ---\n");
    test_path_b_e2e_identity_manipulation_succeeds();
    test_path_b_e2e_activation_rejected_by_nonce_barrier();
    test_path_b_e2e_signal_detection_cellular_vs_wifi();
}
