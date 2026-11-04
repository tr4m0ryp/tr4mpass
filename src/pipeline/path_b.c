/*
 * path_b.c -- Path B pipeline orchestration for A12+ devices.
 *
 * Orchestrates the full A12+ pipeline flow:
 *   1. DFU identity manipulation (serial descriptor PWND marker)
 *   2. Signal type detection (cellular vs WiFi-only)
 *   3. drmHandshake session activation protocol
 *   4. Post-pipeline deletescript cleanup
 *   5. Activation state verification
 *
 * Identity manipulation internals are in path_b_identity.c.
 */

#include <string.h>
#include <unistd.h>
#include <plist/plist.h>
#include <libusb.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libirecovery.h>

#include "pipeline/path_b.h"
#include "pipeline/signal.h"
#include "pipeline/deletescript.h"
#include "activation/activation.h"
#include "activation/session.h"
#include "activation/record.h"
#include "device/device.h"
#include "device/usb_dfu.h"
#include "util/log.h"

/* Polling interval (2 s) and max wait for device mode transitions */
#define REBOOT_POLL_USEC     2000000
#define RECOVERY_WAIT_SECS   60
#define NORMAL_WAIT_SECS     90

/* Apple USB IDs */
#define APPLE_VID_PATH_B     0x05AC
#define RECOVERY_PID_PATH_B  0x1281

/* Forward declarations for module callbacks. */
static int path_b_probe(device_info_t *dev);
static int path_b_execute(device_info_t *dev);

const pipeline_module_t path_b_module = {
    .name        = "path_b_a12plus",
    .description = "A12+ pipeline via identity manipulation + session activation",
    .probe       = path_b_probe,
    .execute     = path_b_execute
};

/*
 * path_b_probe -- Check if this device is eligible for Path B.
 *
 * Path B targets A12+ devices (not checkm8-compatible) that are
 * currently in DFU mode. Returns 1 if compatible, 0 if not, -1 on error.
 */
static int path_b_probe(device_info_t *dev)
{
    if (!dev) {
        log_error("[path_b] NULL device in probe");
        return -1;
    }

    /* A12+ devices are not checkm8-compatible */
    if (dev->checkm8_compatible != 0) {
        log_debug("[path_b] Device is checkm8-compatible (A5-A11), skipping");
        return 0;
    }

    /* Must be in DFU mode for identity manipulation */
    if (dev->is_dfu_mode != 1) {
        log_debug("[path_b] Device not in DFU mode, skipping");
        return 0;
    }

    log_info("[path_b] A12+ device in DFU mode detected -- Path B compatible");
    return 1;
}

/*
 * step_reboot_to_recovery -- Step 1/10: transition device from DFU to
 * recovery mode so that iRecovery setenv commands become available.
 *
 * Sends a DFU_ABORT class request which causes the device to reset.
 * On A12+ this typically lands in recovery mode (PID 0x1281).
 * Polls for recovery mode appearance up to RECOVERY_WAIT_SECS seconds.
 */
static int step_reboot_to_recovery(device_info_t *dev)
{
    libusb_context  *ctx = NULL;
    libusb_device  **devs = NULL;
    ssize_t          count;
    ssize_t          i;
    int              elapsed = 0;
    int              found   = 0;

    log_info("[path_b] Step 1/10: Rebooting device from DFU to recovery mode...");

    if (!dev->usb) {
        log_error("[path_b] No USB handle -- device not in DFU mode");
        return -1;
    }

    /* DFU_ABORT: bmRequestType=0x21 (class, interface, host-to-device),
     * bRequest=DFU_ABORT(6), wValue=0, wIndex=0, wLength=0 */
    libusb_control_transfer(dev->usb, 0x21, 6, 0, 0, NULL, 0, 1000);

    /* Release DFU handle -- device is resetting */
    usb_dfu_close(dev->usb);
    dev->usb         = NULL;
    dev->is_dfu_mode = 0;

    log_info("[path_b] DFU abort sent, waiting for recovery mode...");

    if (libusb_init(&ctx) != LIBUSB_SUCCESS) {
        log_error("[path_b] libusb_init failed for recovery poll");
        return -1;
    }

    while (elapsed < RECOVERY_WAIT_SECS) {
        usleep(REBOOT_POLL_USEC);
        elapsed += 2;

        count = libusb_get_device_list(ctx, &devs);
        if (count < 0) {
            libusb_free_device_list(devs, 1);
            continue;
        }

        for (i = 0; i < count && !found; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devs[i], &desc) != LIBUSB_SUCCESS)
                continue;
            if (desc.idVendor == APPLE_VID_PATH_B &&
                desc.idProduct == RECOVERY_PID_PATH_B)
                found = 1;
        }

        libusb_free_device_list(devs, 1);

        if (found) {
            log_info("[path_b] Recovery mode detected (%ds)", elapsed);
            libusb_exit(ctx);
            return 0;
        }

        log_debug("[path_b] Waiting for recovery... %ds / %ds",
                  elapsed, RECOVERY_WAIT_SECS);
    }

    libusb_exit(ctx);
    log_error("[path_b] Timed out waiting for recovery mode (%ds)", RECOVERY_WAIT_SECS);
    return -1;
}

/*
 * step_manipulate_identity -- Step 2/10: set PWND marker in serial-number
 * via iRecovery setenv. Device must be in recovery mode at this point
 * (step_reboot_to_recovery() must have run first).
 */
static int step_manipulate_identity(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 2/10: Manipulating device identity in recovery mode...");

    rc = path_b_manipulate_identity(dev);
    if (rc != 0) {
        log_error("[path_b] Identity manipulation failed");
        return -1;
    }

    log_info("[path_b] Identity manipulation succeeded");
    return 0;
}

/*
 * step_reboot_to_normal -- Step 3/10: reboot from recovery to normal iOS
 * and reconnect via lockdownd so activation services become available.
 *
 * Sends "reboot" via iRecovery, then polls for normal mode (idevice_id).
 * Repopulates dev->handle and dev->lockdown on success.
 */
static int step_reboot_to_normal(device_info_t *dev)
{
    irecv_client_t  client  = NULL;
    irecv_error_t   err;
    char          **devices = NULL;
    int             count   = 0;
    int             elapsed = 0;

    log_info("[path_b] Step 3/10: Rebooting from recovery to normal iOS...");

    /* Open recovery device to send reboot command */
    if (dev->ecid != 0)
        err = irecv_open_with_ecid_and_attempts(&client, (uint64_t)dev->ecid, 5);
    else
        err = irecv_open_with_ecid_and_attempts(&client, 0, 5);

    if (err != IRECV_E_SUCCESS || !client) {
        log_error("[path_b] Could not open iRecovery for reboot: %s",
                  irecv_strerror(err));
        return -1;
    }

    err = irecv_reboot(client);
    irecv_close(client);

    if (err != IRECV_E_SUCCESS)
        log_warn("[path_b] iRecovery reboot command returned: %s (continuing)",
                 irecv_strerror(err));
    else
        log_info("[path_b] Reboot command sent, waiting for normal iOS mode...");

    /* Poll for normal mode */
    while (elapsed < NORMAL_WAIT_SECS) {
        usleep(REBOOT_POLL_USEC);
        elapsed += 2;

        if (idevice_get_device_list(&devices, &count) == IDEVICE_E_SUCCESS
            && count > 0) {
            idevice_device_list_free(devices);
            log_info("[path_b] Device visible in normal mode (%ds)", elapsed);
            break;
        }
        if (devices) {
            idevice_device_list_free(devices);
            devices = NULL;
        }
        log_debug("[path_b] Waiting for normal mode... %ds / %ds",
                  elapsed, NORMAL_WAIT_SECS);
    }

    if (elapsed >= NORMAL_WAIT_SECS) {
        log_error("[path_b] Timed out waiting for normal iOS mode (%ds)",
                  NORMAL_WAIT_SECS);
        return -1;
    }

    /* Reconnect via lockdownd to populate dev->handle / dev->lockdown */
    if (device_detect(dev) < 0) {
        log_error("[path_b] device_detect failed after reboot");
        return -1;
    }
    if (device_query_info(dev) < 0)
        log_warn("[path_b] device_query_info incomplete (continuing)");

    log_info("[path_b] Reconnected in normal mode, lockdownd available");
    return 0;
}

/*
 * step_detect_signal -- Steps 4-5: detect and display signal info.
 */
static int step_detect_signal(device_info_t *dev)
{
    signal_type_t sig;

    log_info("[path_b] Step 4/10: Detecting signal type...");

    sig = signal_detect_type(dev);
    (void)sig; /* Used implicitly by signal_print_info */
    signal_print_info(dev);

    return 0;
}

/*
 * step_session_handshake -- Steps 4-6: session info + drmHandshake + activation info.
 * On success, *out_response and *out_info are set (caller must free both).
 */
static int step_session_handshake(device_info_t *dev,
                                  plist_t *out_response,
                                  plist_t *out_info)
{
    plist_t session  = NULL;
    plist_t response = NULL;
    plist_t info     = NULL;
    int     rc;

    /* Step 5: get session info blob from device */
    log_info("[path_b] Step 5/10: Requesting session info...");
    rc = session_get_info(dev, &session);
    if (rc != 0 || !session) {
        log_error("[path_b] Failed to get session info");
        return -1;
    }
    log_info("[path_b] Session info retrieved");

    /* Step 6: perform local drmHandshake */
    log_info("[path_b] Step 6/10: Performing drmHandshake...");
    rc = session_drm_handshake(dev, session, &response);
    if (rc != 0 || !response) {
        log_error("[path_b] drmHandshake failed");
        plist_free(session);
        return -1;
    }
    log_info("[path_b] drmHandshake succeeded");

    /* Step 7: create activation info with session response */
    log_info("[path_b] Step 7/10: Creating activation info...");
    rc = session_create_activation_info(dev, response, &info);
    if (rc != 0 || !info) {
        log_error("[path_b] Failed to create activation info");
        plist_free(session);
        plist_free(response);
        return -1;
    }
    log_info("[path_b] Activation info created");

    plist_free(session);

    *out_response = response;
    *out_info     = info;
    return 0;
}

/*
 * step_activate -- Step 7: build A12+ record and activate with session.
 */
static int step_activate(device_info_t *dev, plist_t response)
{
    plist_t record = NULL;
    int     rc;

    log_info("[path_b] Step 8/10: Building A12+ activation record...");
    record = record_build_a12(dev);
    if (!record) {
        log_error("[path_b] Failed to build A12+ activation record");
        return -1;
    }
    log_info("[path_b] Activation record built");

    log_info("[path_b] Activating device with session...");
    rc = session_activate(dev, record, response);
    if (rc != 0) {
        log_error("[path_b] Session activation failed");
        record_free(record);
        return -1;
    }
    log_info("[path_b] Session activation succeeded");

    record_free(record);
    return 0;
}

/*
 * step_deletescript -- Step 8: run post-pipeline cleanup.
 */
static int step_deletescript(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 9/10: Running deletescript cleanup...");

    rc = deletescript_run(dev);
    if (rc != 0) {
        log_warn("[path_b] Deletescript reported errors (non-fatal)");
        /* Continue -- activation may still be valid even if cleanup
         * partially fails (e.g. Setup.app already removed). */
    } else {
        log_info("[path_b] Deletescript completed");
    }

    return 0;
}

/*
 * step_verify -- Step 9: check if activation was successful.
 */
static int step_verify(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 10/10: Verifying activation state...");

    rc = activation_is_activated(dev);
    if (rc == 1) {
        log_info("[path_b] Verification PASSED -- device is activated");
        return 0;
    }

    if (rc == 0) {
        log_error("[path_b] Verification FAILED -- device is NOT activated");
    } else {
        log_error("[path_b] Verification ERROR -- could not query state");
    }

    return -1;
}

/*
 * path_b_execute -- Run the full A12+ pipeline flow.
 *
 * Steps are separated into helper functions for clarity and to keep
 * the top-level flow readable. Each step logs its own progress.
 */
static int path_b_execute(device_info_t *dev)
{
    plist_t response = NULL;
    plist_t info     = NULL;
    int     rc;

    log_info("[path_b] === Starting A12+ pipeline (Path B) ===");
    log_info("[path_b] Device: %s (CPID 0x%04X, ECID 0x%llX)",
             dev->product_type, dev->cpid,
             (unsigned long long)dev->ecid);

    /* Step 1: DFU -> recovery mode transition */
    rc = step_reboot_to_recovery(dev);
    if (rc != 0)
        return -1;

    /* Step 2: set PWND serial marker via iRecovery setenv */
    rc = step_manipulate_identity(dev);
    if (rc != 0)
        return -1;

    /* Step 3: recovery -> normal iOS, reconnect lockdownd */
    rc = step_reboot_to_normal(dev);
    if (rc != 0)
        return -1;

    /* Step 4: signal detection (lockdownd now available) */
    rc = step_detect_signal(dev);
    if (rc != 0)
        return -1;

    /* Steps 5-7: session handshake */
    rc = step_session_handshake(dev, &response, &info);
    if (rc != 0)
        return -1;

    /* Step 8: build record + activate */
    rc = step_activate(dev, response);
    if (rc != 0) {
        plist_free(response);
        plist_free(info);
        return -1;
    }

    /* Free session plists before proceeding to cleanup */
    plist_free(response);
    plist_free(info);

    /* Step 8: deletescript cleanup */
    step_deletescript(dev);

    /* Step 9: verify activation */
    rc = step_verify(dev);

    if (rc == 0) {
        log_info("[path_b] === A12+ pipeline completed successfully ===");
    } else {
        log_error("[path_b] === A12+ pipeline FAILED at verification ===");
    }

    return rc;
}
