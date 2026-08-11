/*
 * path_a_ssh.c -- SSH/SCP jailbreak layer for Path A.
 *
 * After the ramdisk boots with dropbear listening on localhost:2222,
 * this module opens an SSH session and runs the commands that remount
 * the rootfs rw, apply AMFI patches, and stage mobileactivationd
 * replacement.  The low-level libssh2 plumbing lives in
 * path_a_ssh_helpers.c; everything here is flow control.
 */

#include "bypass/path_a_internal.h"
#include "bypass/path_a_ssh_internal.h"
#include "device/device.h"
#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAD_REMOTE_PATH   "/mnt1/usr/libexec/mobileactivationd"
#define MAD_BACKUP_CMD    "cp " MAD_REMOTE_PATH " " MAD_REMOTE_PATH ".bak"
#define MAD_CHMOD_CMD     "chmod 755 " MAD_REMOTE_PATH
#define MAD_CHOWN_CMD     "chown 0:0 " MAD_REMOTE_PATH
#define MAD_VERIFY_CMD    "stat -c '%a %U:%G' " MAD_REMOTE_PATH

static const char *
env_or(const char *key, const char *fallback)
{
    const char *v = getenv(key);
    return (v && *v) ? v : fallback;
}

static int
env_port_or(const char *key, int fallback)
{
    const char *v = getenv(key);
    if (!v || !*v)
        return fallback;
    char *end = NULL;
    long parsed = strtol(v, &end, 10);
    if (!end || end == v || *end != '\0' || parsed < 1 || parsed > 65535) {
        log_warn("path_a_ssh: invalid port value '%s' for %s, using default %d",
                 v, key, fallback);
        return fallback;
    }
    return (int)parsed;
}

static int
ssh_connect_from_env(ssh_sess_t **sess_out)
{
    const char *host = env_or("TR4MPASS_SSH_HOST", "127.0.0.1");
    const char *user = env_or("TR4MPASS_SSH_USER", "root");
    const char *pw   = env_or("TR4MPASS_SSH_PASSWORD", "alpine");
    int port         = env_port_or("TR4MPASS_SSH_PORT", 2222);
    return ssh_open(sess_out, host, port, user, pw);
}

/*
 * run_required -- Execute a remote command and fail if its exit code is
 * non-zero.  The label is logged with the exit status for diagnostics.
 */
static int
run_required(ssh_sess_t *sess, const char *label, const char *cmd)
{
    int exit_code = -1;

    log_info("path_a_ssh: exec [%s] %s", label, cmd);
    if (ssh_exec(sess, cmd, &exit_code, NULL, 0) != 0) {
        log_error("path_a_ssh: [%s] transport failure", label);
        return -1;
    }
    if (exit_code != 0) {
        log_error("path_a_ssh: [%s] returned exit=%d", label, exit_code);
        return -1;
    }
    return 0;
}

static int
run_optional(ssh_sess_t *sess, const char *label, const char *cmd)
{
    int exit_code = -1;
    if (ssh_exec(sess, cmd, &exit_code, NULL, 0) != 0) {
        log_warn("path_a_ssh: [%s] transport error (continuing)", label);
        return 0;
    }
    if (exit_code != 0)
        log_warn("path_a_ssh: [%s] exit=%d (continuing)", label, exit_code);
    return 0;
}

int
path_a_jailbreak(device_info_t *dev)
{
    ssh_sess_t *sess = NULL;
    int rc = -1;

    (void)dev;

    if (ssh_connect_from_env(&sess) != 0) {
        log_error("path_a_ssh: jailbreak: SSH connect/auth failed");
        return -1;
    }

    /*
     * Remount rootfs read-write.  On apfs-based devices the first call
     * succeeds; on older hfs ramdisks we fall back.  Only declare
     * failure if neither variant mounts the partition.
     */
    {
        int apfs_rc = -1;
        int hfs_rc  = -1;
        ssh_exec(sess,
                 "/sbin/mount -u -w -t apfs /dev/disk0s1s1 /mnt1",
                 &apfs_rc, NULL, 0);
        if (apfs_rc != 0) {
            ssh_exec(sess,
                     "/sbin/mount -u -w -t hfs /dev/disk0s1s1 /mnt1",
                     &hfs_rc, NULL, 0);
        }
        if (apfs_rc != 0 && hfs_rc != 0) {
            log_error("path_a_ssh: mount: apfs=%d hfs=%d (both failed)",
                      apfs_rc, hfs_rc);
            goto done;
        }
    }

    /*
     * nvram auto-boot=false is advisory: some ramdisks lack an nvram
     * helper, so a non-zero exit is logged and ignored.
     */
    run_optional(sess, "nvram", "/usr/bin/nvram auto-boot=false");

    /* AMFI patch.  Honours TR4MPASS_JBINIT_PATCHER if set. */
    {
        const char *jb = getenv("TR4MPASS_JBINIT_PATCHER");
        const char *cmd = (jb && *jb)
            ? jb
            : "/usr/bin/jbinit --patch-amfi /mnt1/usr/lib/dyld";
        if (run_required(sess, "amfi-patch", cmd) != 0)
            goto done;
    }

    /* Verify mobileactivationd is reachable on disk. */
    if (run_required(sess, "verify-mad",
                     "ls -la " MAD_REMOTE_PATH) != 0)
        goto done;

    rc = 0;

done:
    ssh_close(sess);
    return rc;
}

static off_t
file_size_of(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    if (!S_ISREG(st.st_mode))
        return -1;
    return st.st_size;
}

int
path_a_replace_mobileactivationd(device_info_t *dev)
{
    ssh_sess_t *sess = NULL;
    const char *patched;
    char verify_buf[128] = {0};
    int rc = -1;

    (void)dev;

    patched = getenv("TR4MPASS_PATCHED_MAD");
    if (!patched || !*patched) {
        log_error("path_a_ssh: TR4MPASS_PATCHED_MAD is not set");
        return -1;
    }
    if (file_size_of(patched) < 0) {
        log_error("path_a_ssh: patched mad '%s' is missing or not a file",
                  patched);
        return -1;
    }

    if (ssh_connect_from_env(&sess) != 0) {
        log_error("path_a_ssh: replace_mad: SSH connect/auth failed");
        return -1;
    }

    if (run_required(sess, "backup-mad", MAD_BACKUP_CMD) != 0)
        goto done;

    log_info("path_a_ssh: SCP upload '%s' -> '%s'", patched, MAD_REMOTE_PATH);
    if (ssh_scp_upload(sess, patched, MAD_REMOTE_PATH, 0755) != 0) {
        log_error("path_a_ssh: SCP upload of patched mad failed");
        goto done;
    }

    if (run_required(sess, "chmod-mad", MAD_CHMOD_CMD) != 0)
        goto done;
    if (run_required(sess, "chown-mad", MAD_CHOWN_CMD) != 0)
        goto done;

    {
        int exit_code = -1;
        if (ssh_exec(sess, MAD_VERIFY_CMD, &exit_code,
                     verify_buf, sizeof(verify_buf)) != 0) {
            log_error("path_a_ssh: verify stat failed (transport)");
            goto done;
        }
        if (exit_code != 0 || strstr(verify_buf, "755") == NULL) {
            log_error("path_a_ssh: mode verification failed: '%s'",
                      verify_buf);
            goto done;
        }
        log_info("path_a_ssh: verified mad stat: %s", verify_buf);
    }

    rc = 0;

done:
    ssh_close(sess);
    return rc;
}
