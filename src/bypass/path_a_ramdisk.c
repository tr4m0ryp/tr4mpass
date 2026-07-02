/*
 * path_a_ramdisk.c -- Ramdisk delivery over iRecovery after checkm8 pwn.
 *
 * After the checkm8 DFU exploit leaves the device in pwned DFU mode, the
 * BootROM accepts unsigned images. This module walks the device through:
 *
 *   1. Upload iBSS via the pwned DFU stage.
 *   2. Reconnect in recovery mode (USB PID flips from 0x1227 to 0x1281).
 *   3. Upload iBEC, issue "go" to jump into the modified iBEC.
 *   4. Reconnect in the iBEC recovery context.
 *   5. Upload devicetree, (optionally) static trust cache, and ramdisk.
 *   6. Issue "bootx" to boot the ramdisk.
 *   7. Poll 127.0.0.1:<ssh_port> until an SSH-over-USB tunnel responds.
 *
 * SSH polling lives in path_a_ramdisk_poll.c to keep each TU under the
 * 300-line project cap.
 *
 * Paths and tunables come from the environment so the module stays
 * config.c-free (config.c is owned by another phase). Required vars:
 *
 *   TR4MPASS_IBSS_PATH         -- required
 *   TR4MPASS_IBEC_PATH         -- required
 *   TR4MPASS_DEVICETREE_PATH   -- required
 *   TR4MPASS_RAMDISK_PATH      -- required
 *   TR4MPASS_TRUSTCACHE_PATH   -- optional (iOS 14+)
 *   TR4MPASS_SSH_PORT          -- default 2222
 *   TR4MPASS_RAMDISK_TIMEOUT   -- default 120 (seconds)
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libirecovery.h>

#include "bypass/path_a_internal.h"
#include "bypass/path_a_ramdisk_internal.h"
#include "device/device.h"
#include "util/log.h"
#include "compat/libirecovery_compat.h"

#define DEFAULT_SSH_PORT        2222
#define DEFAULT_TIMEOUT_SEC     120
#define RECONNECT_POLL_USEC     500000  /* 0.5 s */
#define POST_CLOSE_USEC         200000  /* 0.2 s */

/* ------------------------------------------------------------------ */
/* Environment helpers                                                */
/* ------------------------------------------------------------------ */

static const char *
env_required(const char *name)
{
    const char *val = getenv(name);
    if (!val || val[0] == '\0') {
        log_error("path_a_ramdisk: %s is not set", name);
        return NULL;
    }
    return val;
}

static int
env_int(const char *name, int fallback)
{
    const char *val = getenv(name);
    long v;
    char *end;

    if (!val || val[0] == '\0')
        return fallback;
    v = strtol(val, &end, 10);
    if (end == val || v <= 0 || v > 0x7FFFFFFF)
        return fallback;
    return (int)v;
}

static int
file_readable(const char *path, const char *label)
{
    if (access(path, R_OK) != 0) {
        log_error("path_a_ramdisk: %s file not readable: %s (%s)",
                  label, path, strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* irecv helpers                                                      */
/* ------------------------------------------------------------------ */

static int
reconnect_device(irecv_client_t *client, uint64_t ecid,
                 int timeout_sec, const char *stage_label)
{
    time_t start, now;
    irecv_error_t err;

    if (!client)
        return -1;

    if (*client) {
        irecv_close(*client);
        *client = NULL;
    }
    usleep(POST_CLOSE_USEC);

    log_info("path_a_ramdisk: waiting for device to reappear (%s)",
             stage_label ? stage_label : "reconnect");

    start = time(NULL);
    for (;;) {
        err = irecv_open_with_ecid(client, ecid);
        if (err == IRECV_E_SUCCESS && *client)
            return 0;

        /* iBEC may briefly disappear; common cases here are
         * IRECV_E_NO_DEVICE / IRECV_E_UNABLE_TO_CONNECT / ENODEV. */
        now = time(NULL);
        if ((long)(now - start) >= (long)timeout_sec) {
            log_error("path_a_ramdisk: reconnect timeout (%s) after %ds:"
                      " %s", stage_label ? stage_label : "reconnect",
                      timeout_sec, irecv_strerror(err));
            return -1;
        }
        usleep(RECONNECT_POLL_USEC);
    }
}

static int
send_staged_image(irecv_client_t client, const char *path,
                  const char *label)
{
    irecv_error_t err;

    if (!client || !path || !label) {
        log_error("path_a_ramdisk: send_staged_image received NULL argument");
        return -1;
    }

    log_info("path_a_ramdisk: sending %s (%s)", label, path);
    /* Use compatibility layer for the DFU notify flag.
     * Modern libirecovery (2024+) doesn't have the named constant,
     * so the compatibility header defines it as 0 if missing. */
    err = irecv_send_file(client, path, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
    if (err != IRECV_E_SUCCESS) {
        log_error("path_a_ramdisk: failed to send %s: %s",
                  label, irecv_strerror(err));
        return -1;
    }
    return 0;
}

static int
send_command(irecv_client_t client, const char *cmd)
{
    irecv_error_t err;

    if (!client || !cmd) {
        log_error("path_a_ramdisk: send_command received NULL argument");
        return -1;
    }
    log_info("path_a_ramdisk: issuing iBoot command: %s", cmd);
    err = irecv_send_command(client, cmd);
    if (err != IRECV_E_SUCCESS) {
        log_error("path_a_ramdisk: command '%s' failed: %s",
                  cmd, irecv_strerror(err));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

int
path_a_load_ramdisk(device_info_t *dev)
{
    const char *ibss, *ibec, *devtree, *ramdisk, *trustcache;
    int ssh_port, rd_timeout;
    uint64_t ecid = 0;
    irecv_client_t client = NULL;
    irecv_error_t err;
    int rc = -1;

    if (!dev) {
        log_error("path_a_ramdisk: called with NULL device");
        return -1;
    }

    ibss    = env_required("TR4MPASS_IBSS_PATH");
    ibec    = env_required("TR4MPASS_IBEC_PATH");
    devtree = env_required("TR4MPASS_DEVICETREE_PATH");
    ramdisk = env_required("TR4MPASS_RAMDISK_PATH");
    if (!ibss || !ibec || !devtree || !ramdisk)
        return -1;

    trustcache = getenv("TR4MPASS_TRUSTCACHE_PATH");
    if (trustcache && trustcache[0] == '\0')
        trustcache = NULL;

    ssh_port   = env_int("TR4MPASS_SSH_PORT",        DEFAULT_SSH_PORT);
    rd_timeout = env_int("TR4MPASS_RAMDISK_TIMEOUT", DEFAULT_TIMEOUT_SEC);

    if (file_readable(ibss,    "iBSS")       != 0) return -1;
    if (file_readable(ibec,    "iBEC")       != 0) return -1;
    if (file_readable(devtree, "devicetree") != 0) return -1;
    if (file_readable(ramdisk, "ramdisk")    != 0) return -1;
    if (trustcache && file_readable(trustcache, "trustcache") != 0)
        return -1;

    ecid = dev->ecid;

    /* Stage 1: pwned DFU -> iBSS upload */
    log_info("path_a_ramdisk: opening pwned DFU (ecid=0x%llx)",
             (unsigned long long)ecid);
    err = irecv_open_with_ecid(&client, ecid);
    if (err != IRECV_E_SUCCESS || !client) {
        log_error("path_a_ramdisk: failed to open pwned DFU: %s",
                  irecv_strerror(err));
        return -1;
    }

    if (send_staged_image(client, ibss, "iBSS") != 0)
        goto cleanup;

    /* Stage 2: reconnect in recovery mode for iBEC upload */
    if (reconnect_device(&client, ecid, rd_timeout, "after iBSS") != 0)
        goto cleanup;

    if (send_staged_image(client, ibec, "iBEC") != 0)
        goto cleanup;

    if (send_command(client, "go") != 0)
        goto cleanup;

    /* Stage 3: reconnect in iBEC context */
    if (reconnect_device(&client, ecid, rd_timeout, "after iBEC go") != 0)
        goto cleanup;

    /* Stage 4: devicetree / trustcache / ramdisk uploads */
    if (send_staged_image(client, devtree, "devicetree") != 0)
        goto cleanup;

    if (trustcache) {
        if (send_staged_image(client, trustcache, "trustcache") != 0)
            goto cleanup;
    } else {
        log_info("path_a_ramdisk: no trustcache configured (pre-iOS 14)");
    }

    if (send_staged_image(client, ramdisk, "ramdisk") != 0)
        goto cleanup;

    /* Stage 5: boot and wait for SSH */
    if (send_command(client, "bootx") != 0)
        goto cleanup;

    irecv_close(client);
    client = NULL;

    if (path_a_ramdisk_poll_ssh(ssh_port, rd_timeout) != 0)
        goto cleanup;

    log_info("path_a_ramdisk: ramdisk booted, SSH reachable on port %d",
             ssh_port);
    rc = 0;

cleanup:
    if (client)
        irecv_close(client);
    return rc;
}
