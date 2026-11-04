/*
 * test_ssh_delivery.c -- SSH delivery delivery tests (Phase 2C).
 *
 * Exercises path_a_deliver() and path_a_replace_mobileactivationd()
 * against the in-process libssh2 mock.  TR4MPASS_* env vars point the
 * SUT at localhost; mock_control.h stages exec / SCP responses.
 *
 * This file is only compiled into `make test-mocks`; the hw-independent
 * `make test` target links tests/mock_section_stubs.c instead.
 */

#include "test_framework.h"

#include "pipeline/path_a_internal.h"
#include "device/device.h"
#include "mocks/mock_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static device_info_t g_dev_zero;

static void ssh_test_setup(void)
{
    mock_reset_all();
    memset(&g_dev_zero, 0, sizeof(g_dev_zero));
    setenv("TR4MPASS_SSH_HOST",     "127.0.0.1", 1);
    setenv("TR4MPASS_SSH_PORT",     "2222",      1);
    setenv("TR4MPASS_SSH_USER",     "root",      1);
    setenv("TR4MPASS_SSH_PASSWORD", "alpine",    1);
}

static void ssh_test_teardown(void)
{
    unsetenv("TR4MPASS_SSH_HOST");
    unsetenv("TR4MPASS_SSH_PORT");
    unsetenv("TR4MPASS_SSH_USER");
    unsetenv("TR4MPASS_SSH_PASSWORD");
    unsetenv("TR4MPASS_PATCHED_MAD");
    unsetenv("TR4MPASS_JBINIT_PATCHER");
}

/* Stage happy-path defaults for every required delivery command. */
static void stage_delivery_success(void)
{
    mock_ssh_set_exec_response("mount -u -w -t apfs", 0, NULL, 0);
    mock_ssh_set_exec_response("nvram auto-boot=false", 0, NULL, 0);
    mock_ssh_set_exec_response("jbinit", 0, NULL, 0);
    mock_ssh_set_exec_response("ls -la /mnt1/usr/libexec/mobileactivationd",
                               0, NULL, 0);
}

/* Stage mobileactivationd replacement commands (no SCP handled here). */
static void stage_replace_mad_success(const char *verify_stdout)
{
    mock_ssh_set_exec_response("cp /mnt1/usr/libexec/mobileactivationd "
                               "/mnt1/usr/libexec/mobileactivationd.bak",
                               0, NULL, 0);
    mock_ssh_set_exec_response("chmod 755", 0, NULL, 0);
    mock_ssh_set_exec_response("chown 0:0", 0, NULL, 0);
    mock_ssh_set_exec_response("stat -c", 0,
                               verify_stdout, strlen(verify_stdout));
}

/* Write a temp file with the given payload; returns strdup'd path. */
static char *make_temp_patched_mad(const void *bytes, size_t len)
{
    char path[128];
    FILE *fp;
    snprintf(path, sizeof(path), "/tmp/tr4mpass_test_mad_%d", (int)getpid());
    fp = fopen(path, "wb");
    if (!fp) return NULL;
    if (len > 0)
        fwrite(bytes, 1, len, fp);
    fclose(fp);
    return strdup(path);
}

static void remove_if_exists(const char *path)
{
    if (path) unlink(path);
}

/* -- Delivery tests ----------------------------------------------- */

static void test_delivery_auth_failure(void)
{
    ssh_test_setup();
    mock_ssh_set_auth_result(-1);
    ASSERT_EQ(path_a_deliver(&g_dev_zero), -1);
    ssh_test_teardown();
}

static void test_delivery_mount_fails(void)
{
    ssh_test_setup();
    /* Both apfs and hfs mount attempts fail; that is a fatal error. */
    mock_ssh_set_exec_response("mount -u -w -t apfs", 1, NULL, 0);
    mock_ssh_set_exec_response("mount -u -w -t hfs",  1, NULL, 0);
    ASSERT_EQ(path_a_deliver(&g_dev_zero), -1);
    ssh_test_teardown();
}

static void test_delivery_happy_path(void)
{
    ssh_test_setup();
    stage_delivery_success();
    ASSERT_EQ(path_a_deliver(&g_dev_zero), 0);
    /* Spot-check that the required remote commands were issued. */
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 1);
    ASSERT_EQ(mock_call_log_contains("mount -u -w -t apfs"),        1);
    ASSERT_EQ(mock_call_log_contains("jbinit"),                     1);
    ASSERT_EQ(mock_call_log_contains("libssh2_session_disconnect"), 1);
    ssh_test_teardown();
}

static void test_delivery_nvram_continues_on_warning(void)
{
    /* nvram auto-boot=false is advisory -- some ramdisks have no nvram
     * helper; exit 1 must NOT fail the delivery (run_optional). */
    ssh_test_setup();
    mock_ssh_set_exec_response("mount -u -w -t apfs", 0, NULL, 0);
    mock_ssh_set_exec_response("nvram auto-boot=false", 1, NULL, 0);
    mock_ssh_set_exec_response("jbinit", 0, NULL, 0);
    mock_ssh_set_exec_response("ls -la /mnt1/usr/libexec/mobileactivationd",
                               0, NULL, 0);
    ASSERT_EQ(path_a_deliver(&g_dev_zero), 0);
    ssh_test_teardown();
}

/* -- Replace mobileactivationd tests ------------------------------- */

static void test_replace_mad_missing_env(void)
{
    ssh_test_setup();
    unsetenv("TR4MPASS_PATCHED_MAD");
    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);
    ssh_test_teardown();
}

static void test_replace_mad_missing_file(void)
{
    ssh_test_setup();
    setenv("TR4MPASS_PATCHED_MAD",
           "/tmp/tr4mpass_test_nope_does_not_exist", 1);
    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);
    ssh_test_teardown();
}

static void test_replace_mad_happy_path(void)
{
    const char payload[] = "patched-mad-binary-contents";
    char *path;
    char *captured_path = NULL;
    size_t captured_bytes = 0;

    ssh_test_setup();
    path = make_temp_patched_mad(payload, sizeof(payload) - 1);
    ASSERT_NOTNULL(path);
    setenv("TR4MPASS_PATCHED_MAD", path, 1);

    stage_replace_mad_success("755 root:wheel\n");
    mock_ssh_expect_scp(&captured_path, &captured_bytes);

    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), 0);

    ASSERT_NOTNULL(captured_path);
    if (captured_path) {
        ASSERT_STREQ(captured_path, "/mnt1/usr/libexec/mobileactivationd");
        free(captured_path);
    }
    ASSERT_EQ(captured_bytes, sizeof(payload) - 1);

    remove_if_exists(path);
    free(path);
    ssh_test_teardown();
}

static void test_replace_mad_backup_fails(void)
{
    char *path;
    ssh_test_setup();
    path = make_temp_patched_mad("x", 1);
    ASSERT_NOTNULL(path);
    setenv("TR4MPASS_PATCHED_MAD", path, 1);
    /* cp fails -> abort before clobbering the binary. */
    mock_ssh_set_exec_response("cp /mnt1/usr/libexec/mobileactivationd "
                               "/mnt1/usr/libexec/mobileactivationd.bak",
                               1, NULL, 0);
    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);
    /* Must NOT have attempted SCP without a backup in place. */
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"), 0);
    remove_if_exists(path);
    free(path);
    ssh_test_teardown();
}

static void test_replace_mad_chmod_fails(void)
{
    char *path;
    char *captured_path = NULL;
    size_t captured_bytes = 0;

    ssh_test_setup();
    path = make_temp_patched_mad("data", 4);
    ASSERT_NOTNULL(path);
    setenv("TR4MPASS_PATCHED_MAD", path, 1);

    mock_ssh_set_exec_response("cp /mnt1/usr/libexec/mobileactivationd "
                               "/mnt1/usr/libexec/mobileactivationd.bak",
                               0, NULL, 0);
    mock_ssh_set_exec_response("chmod 755", 1, NULL, 0);
    mock_ssh_expect_scp(&captured_path, &captured_bytes);

    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);

    if (captured_path) free(captured_path);
    remove_if_exists(path);
    free(path);
    ssh_test_teardown();
}

static void test_replace_mad_scp_fails(void)
{
    /* Point TR4MPASS_PATCHED_MAD at a directory: our local-side stat
     * check in ssh_scp_upload (and the matching guard in
     * path_a_replace_mobileactivationd) rejects non-regular files and
     * returns -1.  This exercises the SCP error path without needing
     * a dedicated scp-fail hook in the libssh2 mock. */
    ssh_test_setup();
    setenv("TR4MPASS_PATCHED_MAD", "/tmp", 1);
    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"), 0);
    ssh_test_teardown();
}

static void test_replace_mad_verify_mode_mismatch(void)
{
    char *path;
    char *captured_path = NULL;
    size_t captured_bytes = 0;

    ssh_test_setup();
    path = make_temp_patched_mad("data", 4);
    ASSERT_NOTNULL(path);
    setenv("TR4MPASS_PATCHED_MAD", path, 1);

    /* All exec commands succeed, but the verify stat reports 644 --
     * the SUT must notice and return -1 so we don't leave a broken
     * mobileactivationd in place. */
    mock_ssh_set_exec_response("cp /mnt1/usr/libexec/mobileactivationd "
                               "/mnt1/usr/libexec/mobileactivationd.bak",
                               0, NULL, 0);
    mock_ssh_set_exec_response("chmod 755", 0, NULL, 0);
    mock_ssh_set_exec_response("chown 0:0", 0, NULL, 0);
    mock_ssh_set_exec_response("stat -c", 0,
                               "644 root:wheel\n", strlen("644 root:wheel\n"));
    mock_ssh_expect_scp(&captured_path, &captured_bytes);

    ASSERT_EQ(path_a_replace_mobileactivationd(&g_dev_zero), -1);

    if (captured_path) free(captured_path);
    remove_if_exists(path);
    free(path);
    ssh_test_teardown();
}

/* -- Section entry ------------------------------------------------- */

void run_ssh_delivery_tests(void)
{
    printf("--- Section 8: SSH delivery ---\n");
    test_delivery_auth_failure();
    test_delivery_mount_fails();
    test_delivery_happy_path();
    test_delivery_nvram_continues_on_warning();
    test_replace_mad_missing_env();
    test_replace_mad_missing_file();
    test_replace_mad_happy_path();
    test_replace_mad_backup_fails();
    test_replace_mad_chmod_fails();
    test_replace_mad_scp_fails();
    test_replace_mad_verify_mode_mismatch();
}
