/*
 * test_activation_online.c -- Bounded post-reboot activation readiness tests.
 *
 * Exercises activation_wait_ready() and the session_get_info() readiness
 * gate against the mobileactivation mock.  No real hardware is involved;
 * the mock intercepts mobileactivation_client_start_service() and
 * mobileactivation_get_activation_state().
 */

#include "test_framework.h"

#include "activation/activation.h"
#include "activation/session.h"
#include "device/device.h"
#include "mocks/mock_control.h"

#include <plist/plist.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A fake non-NULL idevice handle for the readiness probe. */
#define FAKE_HANDLE ((idevice_t)0x1)

/* Count call-log entries whose text contains needle. */
static int count_call_log_entries(const char *needle)
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

static void test_activation_wait_ready_retries_transient_startup_failures(void)
{
    device_info_t dev;
    int rc;

    printf("  [online] wait_ready retries transient startup failures\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.handle = FAKE_HANDLE;

    /* Two staged MUX_ERROR startup failures, then success. */
    mock_mobileactivation_set_start_service_failures(MOBILEACTIVATION_E_MUX_ERROR, 2);

    rc = activation_wait_ready(&dev, 5);
    ASSERT_EQ(rc, 0);

    /* 3 startup calls (2 failures + 1 success) and 1 state query. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 3);
    ASSERT_EQ(count_call_log_entries("mobileactivation_get_activation_state"), 1);
}

static void test_activation_wait_ready_retries_transient_state_failures(void)
{
    device_info_t dev;
    int rc;

    printf("  [online] wait_ready retries transient state failures\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.handle = FAKE_HANDLE;

    /* Two staged REQUEST_FAILED state-query failures, then success. */
    mock_mobileactivation_set_state_query_failures(MOBILEACTIVATION_E_REQUEST_FAILED, 2);

    rc = activation_wait_ready(&dev, 5);
    ASSERT_EQ(rc, 0);

    /* 3 startup calls (one per attempt) and 3 state queries. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 3);
    ASSERT_EQ(count_call_log_entries("mobileactivation_get_activation_state"), 3);
}

static void test_activation_wait_ready_stops_at_attempt_limit(void)
{
    device_info_t dev;
    int rc;

    printf("  [online] wait_ready stops at attempt limit\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.handle = FAKE_HANDLE;

    /* Persistent startup failure; never recovers. */
    mock_mobileactivation_set_start_service_failures(MOBILEACTIVATION_E_MUX_ERROR, 10);

    rc = activation_wait_ready(&dev, 3);
    ASSERT_EQ(rc, -1);

    /* Exactly 3 startup calls, no state queries. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 3);
    ASSERT_EQ(count_call_log_entries("mobileactivation_get_activation_state"), 0);
}

static void test_activation_wait_ready_does_not_retry_unknown_request(void)
{
    device_info_t dev;
    int rc;

    printf("  [online] wait_ready does not retry UNKNOWN_REQUEST\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.handle = FAKE_HANDLE;

    /* One staged UNKNOWN_REQUEST state-query failure (permanent). */
    mock_mobileactivation_set_state_query_failures(MOBILEACTIVATION_E_UNKNOWN_REQUEST, 1);

    rc = activation_wait_ready(&dev, 5);
    ASSERT_EQ(rc, -1);

    /* Exactly 1 startup call and 1 state-query call. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 1);
    ASSERT_EQ(count_call_log_entries("mobileactivation_get_activation_state"), 1);
}

static void test_activation_wait_ready_rejects_invalid_inputs(void)
{
    device_info_t dev;
    int rc;

    printf("  [online] wait_ready rejects invalid inputs\n");

    mock_reset_all();

    /* NULL device. */
    rc = activation_wait_ready(NULL, 5);
    ASSERT_EQ(rc, -1);

    /* NULL handle. */
    memset(&dev, 0, sizeof(dev));
    rc = activation_wait_ready(&dev, 5);
    ASSERT_EQ(rc, -1);

    /* max_attempts == 0. */
    dev.handle = FAKE_HANDLE;
    rc = activation_wait_ready(&dev, 0);
    ASSERT_EQ(rc, -1);

    /* No service calls at all. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 0);
    ASSERT_EQ(count_call_log_entries("mobileactivation_get_activation_state"), 0);
}

static void test_session_get_info_waits_for_service_readiness(void)
{
    device_info_t dev;
    plist_t session_info = NULL;
    int rc;

    printf("  [online] session_get_info waits for service readiness\n");

    mock_reset_all();
    memset(&dev, 0, sizeof(dev));
    dev.handle = FAKE_HANDLE;

    /* One staged startup failure, then success. */
    mock_mobileactivation_set_start_service_failures(MOBILEACTIVATION_E_MUX_ERROR, 1);

    rc = session_get_info(&dev, &session_info);
    ASSERT_EQ(rc, 0);
    ASSERT_NOTNULL(session_info);

    /* 3 startup calls: 2 readiness attempts (1 failure + 1 success) plus
     * the session request itself. */
    ASSERT_EQ(count_call_log_entries("mobileactivation_client_start_service"), 3);

    plist_free(session_info);
}

void run_activation_online_tests(void)
{
    test_activation_wait_ready_retries_transient_startup_failures();
    test_activation_wait_ready_retries_transient_state_failures();
    test_activation_wait_ready_stops_at_attempt_limit();
    test_activation_wait_ready_does_not_retry_unknown_request();
    test_activation_wait_ready_rejects_invalid_inputs();
    test_session_get_info_waits_for_service_readiness();
}
