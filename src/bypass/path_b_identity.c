/*
 * path_b_identity.c -- Recovery identity manipulation for A12+ devices.
 *
 * Reads the iBoot identity through libirecovery, appends the
 * PWND:[checkm8] marker, and writes the serial-number environment
 * variable while the device is in Recovery mode.
 */

#include <string.h>
#include <stdio.h>
#include "compat/libirecovery_compat.h"

#include "bypass/path_b.h"
#include "device/usb_dfu.h"
#include "util/log.h"

/* PWND marker appended to the serial string */
#define PWND_MARKER             " PWND:[checkm8]"

/*
 * path_b_read_serial -- Read the device identity through libirecovery.
 * Recovery exposes the identity as serial_string in irecv_device_info.
 */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;
    const struct irecv_device_info *info;

    if (!dev || !buf || len == 0) {
        log_error("[path_b_id] Invalid arguments to read_serial");
        return -1;
    }

    if (dev->ecid != 0)
        err = irecv_open_with_ecid_and_attempts(&client,
                                                (uint64_t)dev->ecid, 5);
    else
        err = irecv_open_with_ecid_and_attempts(&client, 0, 5);

    if (err != IRECV_E_SUCCESS || !client) {
        log_error("[path_b_id] iRecovery open failed for serial read: %s",
                  irecv_strerror(err));
        return -1;
    }

    info = irecv_get_device_info(client);
    if (!info || !info->serial_string || info->serial_string[0] == '\0') {
        log_error("[path_b_id] iRecovery: no serial string available");
        irecv_close(client);
        return -1;
    }

    strncpy(buf, info->serial_string, len - 1);
    buf[len - 1] = '\0';
    log_debug("[path_b_id] Read serial (Recovery): %s", buf);
    irecv_close(client);
    return 0;
}

/*
 * path_b_write_serial_irecovery -- Set serial-number env var in recovery mode.
 *
 * Apple's A12+ BootROM rejects SET_DESCRIPTOR with LIBUSB_ERROR_PIPE.
 * Instead, this function uses libirecovery to send "setenv serial-number"
 * from recovery mode (PID 0x1281), which iBoot accepts before booting iOS.
 * The PWND marker in the serial is then visible to mobileactivationd.
 *
 * Device must already be in recovery mode before this is called.
 * path_b.c:step_reboot_to_recovery() handles the DFU -> recovery transition.
 */
int path_b_write_serial_irecovery(device_info_t *dev, const char *new_serial)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;
    int            mode = 0;
    int            rc = -1;

    if (!dev || !new_serial) {
        log_error("[path_b_id] Invalid arguments to write_serial_irecovery");
        return -1;
    }

    /* Open device -- prefer ECID match to avoid touching wrong device.
     * irecv_open_with_ecid with ecid=0 matches any connected device. */
    if (dev->ecid != 0)
        err = irecv_open_with_ecid_and_attempts(&client, (uint64_t)dev->ecid, 5);
    else
        err = irecv_open_with_ecid_and_attempts(&client, 0, 5);

    if (err != IRECV_E_SUCCESS || !client) {
        log_error("[path_b_id] Could not open device in recovery mode: %s",
                  irecv_strerror(err));
        return -1;
    }

    /* Verify the device is actually in recovery (not DFU or normal).
     * struct irecv_device_info has no pid field on any released
     * libirecovery; query the mode via irecv_get_mode() instead and
     * accept any of the four recovery-mode PIDs (0x1280..0x1283). */
    err = irecv_get_mode(client, &mode);
    if (err != IRECV_E_SUCCESS ||
        mode < IRECV_K_RECOVERY_MODE_1 || mode > IRECV_K_RECOVERY_MODE_4) {
        log_error("[path_b_id] Device is not in recovery mode (mode=0x%04X, err=%s)",
                  (unsigned)mode, irecv_strerror(err));
        irecv_close(client);
        return -1;
    }

    /* Set the serial-number environment variable */
    err = irecv_setenv(client, "serial-number", new_serial);
    if (err != IRECV_E_SUCCESS) {
        log_error("[path_b_id] irecv_setenv serial-number failed: %s",
                  irecv_strerror(err));
        goto done;
    }
    log_info("[path_b_id] setenv serial-number succeeded");

    /* Persist to NVRAM -- non-fatal if saveenv is unsupported by iBoot */
    err = irecv_saveenv(client);
    if (err != IRECV_E_SUCCESS)
        log_warn("[path_b_id] saveenv failed (%s), continuing (in-session only)",
                 irecv_strerror(err));
    else
        log_info("[path_b_id] Serial persisted to NVRAM via saveenv");

    log_info("[path_b_id] Serial set via iRecovery: %s", new_serial);
    rc = 0;

done:
    irecv_close(client);
    return rc;
}

/*
 * path_b_write_serial -- Write modified serial to the device.
 * Routes through iRecovery setenv (recovery mode) since A12+ BootROM
 * STALLs the USB SET_DESCRIPTOR request.
 */
int path_b_write_serial(device_info_t *dev, const char *new_serial)
{
    if (!dev || !new_serial) {
        log_error("[path_b_id] Invalid arguments to write_serial");
        return -1;
    }

    log_info("[path_b_id] Writing serial via iRecovery (recovery mode)...");
    return path_b_write_serial_irecovery(dev, new_serial);
}

/* path_b_manipulate_identity -- Read, append PWND marker, and write. */
int path_b_manipulate_identity(device_info_t *dev)
{
    char current[DFU_SERIAL_MAX];
    char modified[DFU_SERIAL_MAX];
    int  rc;

    if (!dev) {
        log_error("[path_b_id] NULL device");
        return -1;
    }

    /* Step 1: read current Recovery identity */
    log_info("[path_b_id] Reading current Recovery identity...");
    rc = path_b_read_serial(dev, current, sizeof(current));
    if (rc != 0) {
        log_error("[path_b_id] Cannot read Recovery identity");
        return -1;
    }
    log_info("[path_b_id] Current serial: %s", current);

    /* Step 2: check if already manipulated */
    if (strstr(current, "PWND:") != NULL) {
        log_info("[path_b_id] PWND marker already present -- skipping");
        return 0;
    }

    /* Step 3: build modified serial with PWND marker */
    rc = snprintf(modified, sizeof(modified), "%s%s", current, PWND_MARKER);
    if (rc < 0 || (size_t)rc >= sizeof(modified)) {
        log_error("[path_b_id] Modified serial exceeds buffer size");
        return -1;
    }
    log_info("[path_b_id] Modified serial: %s", modified);

    /* Step 4: write the iBoot environment variable */
    log_info("[path_b_id] Writing modified serial environment variable...");
    rc = path_b_write_serial(dev, modified);
    if (rc != 0) {
        log_error("[path_b_id] Failed to write modified serial");
        return -1;
    }

    log_info("[path_b_id] Identity manipulation completed");
    return 0;
}
