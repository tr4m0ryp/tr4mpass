/*
 * test_path_a_e2e.c -- End-to-end Path A pipeline pipeline tests.
 *
 * Drives path_a_module.execute() against the full mock stack with Phase 2
 * modules linked in.  checkm8 is stubbed by mock_hw_stubs.c so each stage
 * is individually inject-failable.  Six scenarios: happy, stage-fail,
 * ramdisk-timeout, SSH-auth-fail, SCP-fail, missing-env.  Call ordering
 * is verified via the shared mock log (checkm8 -> iBSS -> iBEC -> "go"
 * -> "bootx" -> ssh -> mount -> jbinit -> backup-mad -> SCP).
 */

#include "test_framework.h"
#include "integration_helpers.h"

#include "pipeline/path_a.h"
#include "device/device.h"
#include "mocks/mock_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* IRECV_E_NO_DEVICE numeric equivalent. */
#define E2E_IRECV_E_NO_DEVICE (-1)

static void
stage_happy_ssh(void)
{
    /* path_a_deliver: rw mount, nvram (advisory), amfi-patch, verify. */
    mock_ssh_set_exec_response("mount -u -w -t apfs", 0, NULL, 0);
    mock_ssh_set_exec_response("nvram auto-boot=false", 0, NULL, 0);
    mock_ssh_set_exec_response("jbinit", 0, NULL, 0);
    mock_ssh_set_exec_response("ls -la /mnt1/usr/libexec/mobileactivationd",
                               0, NULL, 0);
    /* path_a_replace_mobileactivationd: backup, chmod, chown, stat. */
    mock_ssh_set_exec_response("cp /mnt1/usr/libexec/mobileactivationd "
                               "/mnt1/usr/libexec/mobileactivationd.bak",
                               0, NULL, 0);
    mock_ssh_set_exec_response("chmod 755", 0, NULL, 0);
    mock_ssh_set_exec_response("chown 0:0", 0, NULL, 0);
    mock_ssh_set_exec_response("stat -c", 0,
                               "755 root:wheel\n",
                               strlen("755 root:wheel\n"));
}

/*
 * test_path_a_e2e_happy_path -- Full pipeline succeeds end-to-end.
 * Asserts execute()==0, every stage's log marker is present, and the
 * inter-stage ordering constraints hold.
 */
static void
test_path_a_e2e_happy_path(void)
{
    device_info_t dev;
    e2e_fixture_t fx;
    int rc;

    printf("  [e2e] path_a happy path\n");

    mock_reset_all();
    ASSERT_EQ(e2e_fixture_init(&fx), 0);
    stage_happy_ssh();
    dev = e2e_make_device_path_a();
    /* deletescript + activation need non-null handle + lockdown; mocks
     * accept any non-null opaque pointer. */
    dev.handle   = (idevice_t)(uintptr_t)0x1;
    dev.lockdown = (lockdownd_client_t)(uintptr_t)0x2;

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, 0);

    ASSERT_EQ(mock_call_log_contains("checkm8_run"),      1);
    ASSERT_EQ(mock_call_log_contains("checkm8_verify"), 1);
    ASSERT_EQ(mock_call_log_contains("irecv_open_with_ecid"), 1);
    ASSERT_EQ(mock_call_log_contains(fx.ibss),                1);
    ASSERT_EQ(mock_call_log_contains(fx.ibec),                1);
    ASSERT_EQ(mock_call_log_contains(fx.devtree),             1);
    ASSERT_EQ(mock_call_log_contains(fx.ramdisk),             1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    1);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 1);
    ASSERT_EQ(mock_call_log_contains("mount -u -w -t apfs"),       1);
    ASSERT_EQ(mock_call_log_contains("jbinit"),                    1);
    ASSERT_EQ(mock_call_log_contains("mobileactivationd.bak"),     1);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"),          1);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 1);
    ASSERT_EQ(mock_call_log_contains(
                  "mobileactivation_get_activation_state"),        1);

    ASSERT_EQ(e2e_ordering_ok("checkm8_run",
                              "irecv_open_with_ecid"),       1);
    ASSERT_EQ(e2e_ordering_ok(fx.ibss, fx.ibec),             1);
    ASSERT_EQ(e2e_ordering_ok("irecv_send_command(go)",
                              "irecv_send_command(bootx)"),  1);
    ASSERT_EQ(e2e_ordering_ok("irecv_send_command(bootx)",
                              "libssh2_userauth_password"),  1);
    ASSERT_EQ(e2e_ordering_ok("mount -u -w -t apfs",
                              "jbinit"),                     1);
    ASSERT_EQ(e2e_ordering_ok("mobileactivationd.bak",
                              "libssh2_scp_send"),           1);

    e2e_fixture_teardown(&fx);
}

/* Stage failure stops the pipeline at step 1 -- no further mock calls. */
static void
test_path_a_e2e_stage_fails(void)
{
    device_info_t dev;
    e2e_fixture_t fx;
    int rc;

    printf("  [e2e] path_a stage failure\n");

    mock_reset_all();
    ASSERT_EQ(e2e_fixture_init(&fx), 0);
    mock_checkm8_set_stage_result(-1);
    dev = e2e_make_device_path_a();

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains("checkm8_run"),       1);
    ASSERT_EQ(mock_call_log_contains("checkm8_verify"),  0);
    ASSERT_EQ(mock_call_log_contains("irecv_open_with_ecid"),  0);
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 0);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"),      0);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 0);

    e2e_fixture_teardown(&fx);
}

/*
 * iBSS uploads, reconnect never succeeds -- pipeline fails at ramdisk.
 * The FIFO returns success once (for the initial processed DFU open), then
 * the sticky flag forces every subsequent open to fail.
 */
static void
test_path_a_e2e_ramdisk_timeout(void)
{
    device_info_t dev;
    e2e_fixture_t fx;
    int rc;

    printf("  [e2e] path_a ramdisk reconnect timeout\n");

    mock_reset_all();
    ASSERT_EQ(e2e_fixture_init(&fx), 0);
    setenv("TR4MPASS_RAMDISK_TIMEOUT", "1", 1);
    mock_irecv_set_open_error(E2E_IRECV_E_NO_DEVICE);
    dev = e2e_make_device_path_a();

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains("irecv_send_command(go)"),    0);
    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 0);
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 0);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 0);

    e2e_fixture_teardown(&fx);
}

/* Ramdisk boots, SSH auth refused -- no SCP attempted. */
static void
test_path_a_e2e_ssh_auth_fails(void)
{
    device_info_t dev;
    e2e_fixture_t fx;
    int rc;

    printf("  [e2e] path_a SSH auth failure\n");

    mock_reset_all();
    ASSERT_EQ(e2e_fixture_init(&fx), 0);
    stage_happy_ssh();
    mock_ssh_set_auth_result(-1);
    dev = e2e_make_device_path_a();

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 1);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"),          0);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 0);

    e2e_fixture_teardown(&fx);
}

/* Delivery OK, SCP upload fails -- activation never attempted. */
static void
test_path_a_e2e_scp_fails(void)
{
    device_info_t dev;
    e2e_fixture_t fx;
    int rc;

    printf("  [e2e] path_a SCP upload failure\n");

    mock_reset_all();
    ASSERT_EQ(e2e_fixture_init(&fx), 0);
    stage_happy_ssh();
    mock_ssh_force_scp_error(1);
    dev = e2e_make_device_path_a();

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains("irecv_send_command(bootx)"), 1);
    ASSERT_EQ(mock_call_log_contains("jbinit"),                    1);
    ASSERT_EQ(mock_call_log_contains("mobileactivationd.bak"),     1);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"),          1);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 0);

    e2e_fixture_teardown(&fx);
}

/* Env unset -- pipeline fails early, no iRecovery/SSH traffic. */
static void
test_path_a_e2e_missing_env(void)
{
    device_info_t dev;
    int rc;

    printf("  [e2e] path_a missing env vars\n");

    mock_reset_all();
    e2e_unset_all_env();
    dev = e2e_make_device_path_a();

    rc = path_a_module.execute(&dev);
    ASSERT_EQ(rc, -1);

    ASSERT_EQ(mock_call_log_contains("irecv_open_with_ecid"),      0);
    ASSERT_EQ(mock_call_log_contains("libssh2_userauth_password"), 0);
    ASSERT_EQ(mock_call_log_contains("libssh2_scp_send"),          0);
    ASSERT_EQ(mock_call_log_contains("mobileactivation_activate"), 0);
}

void
run_path_a_integration_tests(void)
{
    printf("--- Section 10: Path A end-to-end integration ---\n");
    test_path_a_e2e_happy_path();
    test_path_a_e2e_stage_fails();
    test_path_a_e2e_ramdisk_timeout();
    test_path_a_e2e_ssh_auth_fails();
    test_path_a_e2e_scp_fails();
    test_path_a_e2e_missing_env();
}
