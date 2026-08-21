/*
 * path_b.c -- Path B bypass orchestration for A12+ devices.
 *
 * Orchestrates the full A12+ bypass flow:
 *   1. DFU identity manipulation (serial descriptor PWND marker)
 *   2. Signal type detection (cellular vs WiFi-only)
 *   3. drmHandshake session activation protocol
 *   4. Post-bypass deletescript cleanup
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

#include "bypass/path_b.h"
#include "bypass/signal.h"
#include "bypass/deletescript.h"
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

const bypass_module_t path_b_module = {
    .name        = "path_b_a12plus",
    .description = "A12+ bypass via identity manipulation + session activation",
    .probe       = path_b_probe,
    .execute     = path_b_execute
};

/*
 * path_b_probe -- Check if this device is eligible for Path B.
 *
 * Path B targets A12+ devices (not checkm8-vulnerable) that are
 * currently in DFU mode. Returns 1 if compatible, 0 if not, -1 on error.
 */
static int path_b_probe(device_info_t *dev)
{
    if (!dev) {
        log_error("[path_b] NULL device in probe");
        return -1;
    }

    /* A12+ devices have checkm8_vulnerable == 0 */
    if (dev->checkm8_vulnerable != 0) {
        log_debug("[path_b] Device is checkm8-vulnerable (A5-A11), skipping");
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
 * step_check_jailbreak -- Check if device has patched mobileactivationd.
 *
 * Path B REQUIRES a jailbroken device with patched mobileactivationd.
 * Without it, the session activation will always fail with error -5.
 * 
 * IMPORTANT: Must be called AFTER step_reboot_to_normal() because
 * session_get_info() needs dev->handle (idevice connection) which
 * only exists in Normal Mode, not DFU.
 */
static int step_check_jailbreak(device_info_t *dev)
{
    plist_t state = NULL;
    plist_t jb_marker = NULL;
    int is_jailbroken = 0;

    log_info("[path_b] Step 4/10: Checking jailbreak status...");

    if (!dev || !dev->handle) {
        log_error("[path_b] No device handle -- not connected in normal mode");
        return -1;
    }

    if (session_get_info(dev, &state) != 0 || !state) {
        log_error("[path_b] Cannot retrieve session info -- device not ready");
        return -1;
    }

    /*
     * On a patched mobileactivationd, the session_info often contains
     * additional debug/diagnostic fields. On stock iOS, serverKP is
     * typically present in the handshake but not in the initial session info.
     * Its absence is a hint, but not conclusive.
     */
    jb_marker = plist_dict_get_item(state, "serverKP");
    if (jb_marker) {
        log_info("[path_b] serverKP present in session info -- daemon may be patched");
        is_jailbroken = 1;
    } else {
        log_warn("[path_b] serverKP NOT in session info -- daemon likely STOCK");
        log_warn("[path_b] Path B requires patched mobileactivationd (jailbreak)");
    }

    /*
     * Additional check: Look for FairPlay certificate chain structure.
     */
    plist_t cert_node = plist_dict_get_item(state, "ActivationCert");
    if (cert_node) {
        char *cert_data = NULL;
        uint64_t cert_len = 0;
        plist_get_data_val(cert_node, &cert_data, &cert_len);
        if (cert_data && cert_len > 0) {
            log_debug("[path_b] ActivationCert present (%llu bytes)",
                      (unsigned long long)cert_len);
        }
        if (cert_data) free(cert_data);
    }

    plist_free(state);

    if (!is_jailbroken) {
        log_error("[path_b] PATH B REQUIRES JAILBREAK");
        log_error("[path_b] Your device is NOT jailbroken or mobileactivationd");
        log_error("[path_b] is not patched. Path B cannot work without a");
        log_error("[path_b] patched daemon that accepts local responses.");
        log_error("[path_b] iOS 16.7.2 on A15 is currently NOT jailbreakable.");
        log_error("[path_b] Consider online activation instead.");
        return -1;
    }

    log_info("[path_b] Jailbreak check passed -- proceeding with Path B");
    return 0;
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

    /* Reconnect via lockdownd with retry logic */
    int retry = 0;
    int connected = -1;
    while (retry < 5 && connected != 0) {
        connected = device_detect(dev);
        if (connected != 0) {
            log_debug("[path_b] device_detect attempt %d failed, retrying...", retry + 1);
            sleep(3);
            retry++;
        }
    }
    if (connected != 0) {
        log_error("[path_b] device_detect failed after %d attempts", retry);
        return -1;
    }

    if (device_query_info(dev) < 0)
        log_warn("[path_b] device_query_info incomplete (continuing)");

    /* Verbinde zu lockdownd */
    if (device_connect_lockdownd(dev) != 0) {
        log_error("[path_b] Failed to connect to lockdownd");
        return -1;
    }
    log_info("[path_b] Reconnected in normal mode, lockdownd available");
    return 0;
}

/*
 * step_detect_signal -- Step 5/10: detect and display signal info.
 */
static int step_detect_signal(device_info_t *dev)
{
    signal_type_t sig;

    log_info("[path_b] Step 5/10: Detecting signal type...");

    sig = signal_detect_type(dev);
    (void)sig; /* Used implicitly by signal_print_info */
    signal_print_info(dev);

    return 0;
}

/*
 * step_session_handshake -- Steps 6-8: session info + drmHandshake + activation info.
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

    /* Step 6: get session info blob from device */
    log_info("[path_b] Step 6/10: Requesting session info...");
    rc = session_get_info(dev, &session);
    if (rc != 0 || !session) {
        log_error("[path_b] Failed to get session info");
        return -1;
    }
    log_info("[path_b] Session info retrieved");

    /* Step 7: perform local drmHandshake */
    log_info("[path_b] Step 7/10: Performing drmHandshake...");
    rc = session_drm_handshake(dev, session, &response);
    if (rc != 0 || !response) {
        log_error("[path_b] drmHandshake failed");
        plist_free(session);
        return -1;
    }
    log_info("[path_b] drmHandshake succeeded");

    /* Step 8: create activation info with session response */
    log_info("[path_b] Step 8/10: Creating activation info...");
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
 * step_activate -- Step 9: build A12+ record and activate with session.
 */
static int step_activate(device_info_t *dev, plist_t response)
{
    plist_t record = NULL;
    int     rc;

    log_info("[path_b] Step 9/10: Building A12+ activation record...");
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
 * step_deletescript -- Step 10: run post-bypass cleanup.
 */
static int step_deletescript(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 10/10: Running deletescript cleanup...");

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
 * step_verify -- Verify activation state (post-cleanup).
 */
static int step_verify(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Verifying activation state...");

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
 * path_b_execute -- Run the full A12+ bypass flow.
 *
 * Steps are separated into helper functions for clarity and to keep
 * the top-level flow readable. Each step logs its own progress.
 */
static int path_b_execute(device_info_t *dev)
{
    plist_t response = NULL;
    plist_t info     = NULL;
    int     rc;

    log_info("[path_b] === Starting A12+ bypass (Path B) ===");
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

    /*
     * NEW: Jailbreak check moved HERE (Step 4).
     * Must be after step_reboot_to_normal() because session_get_info()
     * needs dev->handle (idevice connection) which only exists in Normal Mode.
     */
    rc = step_check_jailbreak(dev);
    if (rc != 0) {
        log_error("[path_b] === Path B aborted: device not jailbroken ===");
        return -1;
    }

    /* Step 5: signal detection (lockdownd now available) */
    rc = step_detect_signal(dev);
    if (rc != 0)
        return -1;

    /* Steps 6-8: session handshake */
    rc = step_session_handshake(dev, &response, &info);
    if (rc != 0)
        return -1;

    /* Step 9: build record + activate */
    rc = step_activate(dev, response);
    if (rc != 0) {
        plist_free(response);
        plist_free(info);
        return -1;
    }

    /* Free session plists before proceeding to cleanup */
    plist_free(response);
    plist_free(info);

    /* Step 10: deletescript cleanup */
    step_deletescript(dev);

    /* Verify activation */
    rc = step_verify(dev);

    if (rc == 0) {
        log_info("[path_b] === A12+ bypass completed successfully ===");
    } else {
        log_error("[path_b] === A12+ bypass FAILED at verification ===");
    }

    return rc;
}
