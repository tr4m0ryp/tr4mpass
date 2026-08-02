/*
 * test_ramdisk.c -- Ramdisk delivery tests (Phase 2B).
 *
 * Exercises path_a_load_ramdisk() against the mocked libirecovery.
 * Uses TR4MPASS_TEST_SSH_READY to short-circuit the SSH poll.
 *
 * NOTE: the current mock irecv API cannot force irecv_send_file() to
 * fail, so tests that need a "send fails mid-flow" scenario fault
 * irecv_open_with_ecid() instead. Phase 3 TODO: add
 * mock_irecv_set_send_file_error() for finer granularity.
 */

#include "test_framework.h"

#include "bypass/path_a_internal.h"
#include "device/device.h"
#include "mocks/mock_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* IRECV_E_NO_DEVICE equivalent, without dragging in libirecovery.h. */
#define FAKE_IRECV_E_NO_DEVICE (-1)

/* ------------------------------------------------------------------ */
/* Fixture                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char ibss[128];
    char ibec[128];
    char devtree[128];
    char trustcache[128];
    char ramdisk[128];
} ramdisk_tmpfiles_t;

static void
unset_ramdisk_env(void)
{
    unsetenv("TR4MPASS_IBSS_PATH");
    unsetenv("TR4MPASS_IBEC_PATH");
    unsetenv("TR4MPASS_DEVICETREE_PATH");
    unsetenv("TR4MPASS_TRUSTCACHE_PATH");
    unsetenv("TR4MPASS_RAMDISK_PATH");
    unsetenv("TR4MPASS_SSH_PORT");
    unsetenv("TR4MPASS_RAMDISK_TIMEOUT");
    unsetenv("TR4MPASS_TEST_SSH_READY");
}

static int
write_tmp_blob(const char *path)
{
    static const unsigned char payload[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(payload, 1, sizeof(payload), f) != sizeof(payload)) {
        fclose(f); unlink(path); return -1;
    }
    fclose(f);
    return 0;
}

static int
fixture_init(ramdisk_tmpfiles_t *tf, int include_trustcache)
{
    int pid = (int)getpid();
    memset(tf, 0, sizeof(*tf));
    snprintf(tf->ibss,       sizeof(tf->ibss),
             "/tmp/tr4mpass_test_ibss_%d",       pid);
    snprintf(tf->ibec,       sizeof(tf->ibec),
             "/tmp/tr4mpass_test_ibec_%d",       pid);
    snprintf(tf->devtree,    sizeof(tf->devtree),
             "/tmp/tr4mpass_test_devtree_%d",    pid);
    snprintf(tf->trustcache, sizeof(tf->trustcache),
             "/tmp/tr4mpass_test_trustcache_%d", pid);
    snprintf(tf->ramdisk,    sizeof(tf->ramdisk),
             "/tmp/tr4mpass_test_ramdisk_%d",    pid);
    if (write_tmp_blob(tf->ibss)    != 0) return -1;
    if (write_tmp_blob(tf->ibec)    != 0) return -1;
    if (write_tmp_blob(tf->devtree) != 0) return -1;
    if (write_tmp_blob(tf->ramdisk) != 0) return -1;
    if (include_trustcache && write_tmp_blob(tf->trustcache) != 0)
        return -1;
    setenv("TR4MPASS_IBSS_PATH",       tf->ibss,    1);
    setenv("TR4MPASS_IBEC_PATH",       tf->ibec,    1);
    setenv("TR4MPASS_DEVICETREE_PATH", tf->devtree, 1);
    setenv("TR4MPASS_RAMDISK_PATH",    tf->ramdisk, 1);
    if (include_trustcache)
        setenv("TR4MPASS_TRUSTCACHE_PATH", tf->trustcache, 1);
    else
        unsetenv("TR4MPASS_TRUSTCACHE_PATH");
    return 0;
}

static void
fixture_teardown(ramdisk_tmpfiles_t *tf)
{
    unlink(tf->ibss);
    unlink(tf->ibec);
    unlink(tf->devtree);
    unlink(tf->trustcache);
    unlink(tf->ramdisk);
    unset_ramdisk_env();
}

static device_info_t
make_ramdisk_dev(void)
{
    device_info_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.checkm8_vulnerable = 1;
    dev.is_dfu_mode        = 1;
    dev.ecid               = 0x0012345678ABCDEFULL;
    snprintf(dev.product_type, sizeof(dev.product_type), "iPhone10,3");
    return dev;
}

/* ------------------------------------------------------------------ */
/* Test cases                                                          */
/* ------------------------------------------------------------------ */

static void
test_ramdisk_missing_env_vars(void)
{
    device_info_t dev = make_ramdisk_dev();
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_open_with_ecid"), 0);
}

static void
test_ramdisk_missing_ibss_file(void)
{
    device_info_t dev = make_ramdisk_dev();
    mock_reset_all();
    unset_ramdisk_env();
    setenv("TR4MPASS_IBSS_PATH",       "/tmp/tr4mpass_does_not_exist", 1);
    setenv("TR4MPASS_IBEC_PATH",       "/bin/sh", 1);
    setenv("TR4MPASS_DEVICETREE_PATH", "/bin/sh", 1);
    setenv("TR4MPASS_RAMDISK_PATH",    "/bin/sh", 1);
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_open_with_ecid"), 0);
    unset_ramdisk_env();
}

static void
test_ramdisk_happy_path(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 1), 0);
    setenv("TR4MPASS_TEST_SSH_READY", "1", 1);
    ASSERT_EQ(path_a_load_ramdisk(&dev), 0);
    ASSERT_EQ(mock_call_log_contains("irecv_send_file"), 1);
    ASSERT_EQ(mock_call_log_contains(tf.ibss),       1);
    ASSERT_EQ(mock_call_log_contains(tf.ibec),       1);
    ASSERT_EQ(mock_call_log_contains(tf.devtree),    1);
    ASSERT_EQ(mock_call_log_contains(tf.trustcache), 1);
    ASSERT_EQ(mock_call_log_contains(tf.ramdisk),    1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    fixture_teardown(&tf);
}

static void
test_ramdisk_ibss_send_fails(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 0), 0);
    setenv("TR4MPASS_TEST_SSH_READY", "1", 1);
    /* Mock cannot fault irecv_send_file(); fault the open instead. */
    mock_irecv_set_open_error(FAKE_IRECV_E_NO_DEVICE);
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    0);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 0);
    fixture_teardown(&tf);
}

static void
test_ramdisk_ibec_command_fails(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 0), 0);
    setenv("TR4MPASS_TEST_SSH_READY", "1", 1);
    /* "go" will not contain this substring -> command returns -1. */
    mock_irecv_expect_command("IMPOSSIBLE_SENTINEL");
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 0);
    ASSERT_EQ(mock_call_log_contains(tf.ramdisk),                  0);
    fixture_teardown(&tf);
}

static void
test_ramdisk_reconnect_timeout(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 0), 0);
    setenv("TR4MPASS_TEST_SSH_READY",  "1", 1);
    setenv("TR4MPASS_RAMDISK_TIMEOUT", "1", 1);
    mock_irecv_set_open_error(FAKE_IRECV_E_NO_DEVICE);
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    0);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 0);
    fixture_teardown(&tf);
}

static void
test_ramdisk_ssh_poll_timeout(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 0), 0);
    setenv("TR4MPASS_RAMDISK_TIMEOUT", "1", 1);
    setenv("TR4MPASS_TEST_SSH_READY",  "0", 1);
    ASSERT_EQ(path_a_load_ramdisk(&dev), -1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    ASSERT_EQ(mock_call_log_contains(tf.ramdisk), 1);
    fixture_teardown(&tf);
}

static void
test_ramdisk_trustcache_optional(void)
{
    device_info_t dev = make_ramdisk_dev();
    ramdisk_tmpfiles_t tf;
    mock_reset_all();
    unset_ramdisk_env();
    ASSERT_EQ(fixture_init(&tf, 0), 0);  /* no trustcache */
    setenv("TR4MPASS_TEST_SSH_READY", "1", 1);
    ASSERT_EQ(path_a_load_ramdisk(&dev), 0);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    ASSERT_EQ(mock_call_log_contains(tf.trustcache), 0);
    fixture_teardown(&tf);
}

void run_ramdisk_tests(void)
{
    printf("--- Section 7: Ramdisk delivery ---\n");
    test_ramdisk_missing_env_vars();
    test_ramdisk_missing_ibss_file();
    test_ramdisk_happy_path();
    test_ramdisk_ibss_send_fails();
    test_ramdisk_ibec_command_fails();
    test_ramdisk_reconnect_timeout();
    test_ramdisk_ssh_poll_timeout();
    test_ramdisk_trustcache_optional();
}
