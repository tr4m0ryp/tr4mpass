/*
 * session.c -- drmHandshake session activation protocol.
 *
 * Implements the 5-stage session-mode activation flow (iOS 10.2+).
 * Protocol details extracted from go-ios source analysis.
 *
 * ONLINE flow (for reference):
 *   Stage 1: CreateTunnel1SessionInfoRequest -> HandshakeRequestMessage
 *   Stage 2: POST drmHandshake to albert.apple.com
 *   Stage 3: CreateActivationInfoRequest (BasebandWaitCount=90)
 *   Stage 4: POST deviceActivation to albert.apple.com
 *   Stage 5: HandleActivationInfoWithSessionRequest
 *
 * OFFLINE flow (what this tool implements, per design decision D2):
 *   Stages 1, 3, 5: same device-side plist commands
 *   Stages 2, 4: construct responses locally (no apple.com calls)
 *   Requires prior jailbreak (Path A) or identity manipulation (Path B)
 *   so mobileactivationd accepts locally-crafted responses.
 */

#include <stdlib.h>
#include <string.h>

#include <libimobiledevice/mobileactivation.h>
#include "activation/session.h"
#include "activation/activation.h"
#include "util/log.h"
#include "util/plist_helpers.h"

#define LOG_TAG "[session]"

/* --- Protocol constants (from go-ios) --- */

/*
 * Apple activation server endpoints (online mode only, documented here
 * for reference -- this tool does NOT contact these servers).
 */
#define APPLE_DRM_HANDSHAKE_URL \
    "https://albert.apple.com/deviceservices/drmHandshake"
#define APPLE_DEVICE_ACTIVATION_URL \
    "https://albert.apple.com/deviceservices/deviceActivation"

/* HTTP headers used by the online protocol */
#define ACTIVATION_USER_AGENT \
    "iOS Device Activator (MobileActivation-592.103.2)"
#define CONTENT_TYPE_APPLE_PLIST  "application/x-apple-plist"
#define CONTENT_TYPE_URL_ENCODED  "application/x-www-form-urlencoded"

/* Stage 3 option: how many times to retry baseband activation check */
#define BASEBAND_WAIT_COUNT  90

/* --- Internal helpers --- */

/*
 * Start mobileactivation service and return a client handle.
 * Caller must call mobileactivation_client_free() when done.
 */
static int ma_session_start(device_info_t *dev, mobileactivation_client_t *ma)
{
    mobileactivation_error_t err;

    if (!dev || !dev->handle || !ma) {
        log_error("%s NULL device or handle", LOG_TAG);
        return -1;
    }

    err = mobileactivation_client_start_service(dev->handle, ma, "tr4mpass");
    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s Failed to start mobileactivation service (error %d)",
                  LOG_TAG, (int)err);
        return -1;
    }

    return 0;
}

/*
 * Build handshake response for offline mode (Stage 2 replacement).
 * Online: POST HandshakeRequestMessage to APPLE_DRM_HANDSHAKE_URL
 *   Content-Type: application/x-apple-plist, Accept: application/xml
 *   User-Agent: ACTIVATION_USER_AGENT, Timeout: 5s
 * Offline: construct locally -- patched mobileactivationd accepts it.
 */
static plist_t build_offline_handshake(device_info_t *dev,
                                       plist_t session_info)
{
    plist_t response = NULL;
    plist_t fdr_blob = NULL;
    plist_t su_info = NULL;
    plist_t hrm_node = NULL;

    if (!dev || !session_info) return NULL;

    response = plist_new_dict();
    if (!response) return NULL;

    /* Extract HandshakeRequestMessage -- online would POST this to apple.com,
     * offline echoes it back (patched daemon skips crypto validation). */
    hrm_node = plist_dict_get_item(session_info, "HandshakeRequestMessage");

    /* FDRBlob: empty in offline mode -- patched daemon skips FDR check */
    fdr_blob = plist_new_data("", 0);
    plist_dict_set_item(response, "FDRBlob", fdr_blob);

    /* SUInfo: empty dict -- not required for offline activation */
    su_info = plist_new_dict();
    plist_dict_set_item(response, "SUInfo", su_info);

    /* HandshakeResponseMessage: echo request back (accepted post-patch) */
    if (hrm_node) {
        plist_dict_set_item(response, "HandshakeResponseMessage",
                            plist_copy(hrm_node));
    } else {
        plist_dict_set_item(response, "HandshakeResponseMessage",
                            plist_new_data("", 0));
    }

    /* serverKP: empty -- session encryption bypassed post-patch */
    plist_dict_set_item(response, "serverKP",
                        plist_new_data("", 0));

    /* Carry device UDID through the session */
    plist_dict_set_item(response, "UniqueDeviceID",
                        plist_new_string(dev->udid));

    log_info("%s Built offline handshake response for UDID %s",
             LOG_TAG, dev->udid);

    return response;
}

/* --- Public API --- */

int session_get_info(device_info_t *dev, plist_t *session_info)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;

    if (!dev || !session_info) {
        log_error("%s Invalid arguments to session_get_info", LOG_TAG);
        return -1;
    }

    *session_info = NULL;

    if (ma_session_start(dev, &ma) < 0)
        return -1;

    /*
     * CreateTunnel1SessionInfoRequest: asks the device to produce a
     * session blob containing the CollectionBlob and
     * HandshakeRequestMessage needed for drmHandshake.
     */
    err = mobileactivation_create_activation_session_info(ma, session_info);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS || !*session_info) {
        log_error("%s create_activation_session_info failed (error %d)",
                  LOG_TAG, (int)err);
        *session_info = NULL;
        return -1;
    }

    log_info("%s Retrieved session info from device", LOG_TAG);
    return 0;
}

int session_drm_handshake(device_info_t *dev, plist_t session_info,
                          plist_t *handshake_response)
{
    if (!dev || !session_info || !handshake_response) {
        log_error("%s Invalid arguments to session_drm_handshake", LOG_TAG);
        return -1;
    }

    *handshake_response = NULL;

    /*
     * Stage 2: drmHandshake.
     *
     * ONLINE: POST HandshakeRequestMessage (binary) to
     *   APPLE_DRM_HANDSHAKE_URL with headers:
     *     Content-Type: application/x-apple-plist
     *     Accept: application/xml
     *     User-Agent: ACTIVATION_USER_AGENT
     *   Timeout 5s. Response is XML plist with handshake data.
     *
     * OFFLINE (this path): construct response locally.
     *   session_info contains:
     *     - CollectionBlob: FairPlay DRM collection data
     *     - HandshakeRequestMessage: device-side handshake initiation
     *     - UniqueDeviceID: device UDID
     */
    *handshake_response = build_offline_handshake(dev, session_info);
    if (!*handshake_response) {
        log_error("%s Failed to build offline handshake response", LOG_TAG);
        return -1;
    }

    log_info("%s drmHandshake completed (offline mode)", LOG_TAG);
    return 0;
}

int session_create_activation_info(device_info_t *dev,
                                   plist_t handshake_response,
                                   plist_t *activation_info)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;

    if (!dev || !handshake_response || !activation_info) {
        log_error("%s Invalid arguments to "
                  "session_create_activation_info", LOG_TAG);
        return -1;
    }

    *activation_info = NULL;

    if (ma_session_start(dev, &ma) < 0)
        return -1;

    /*
     * Stage 3: CreateActivationInfoRequest.
     * Passes the handshake response (FDRBlob, SUInfo,
     * HandshakeResponseMessage, serverKP) plus options:
     *   BasebandWaitCount = 90 (retry count for baseband check)
     * Device returns the activation information plist.
     */

    /* Inject BasebandWaitCount option into handshake_response */
    plist_dict_set_item(handshake_response, "BasebandWaitCount",
                        plist_new_uint(BASEBAND_WAIT_COUNT));

    err = mobileactivation_create_activation_info_with_session(
            ma, handshake_response, activation_info);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS || !*activation_info) {
        log_error("%s create_activation_info_with_session failed "
                  "(error %d)", LOG_TAG, (int)err);
        *activation_info = NULL;
        return -1;
    }

    log_info("%s Created session activation info", LOG_TAG);
    return 0;
}

int session_activate(device_info_t *dev, plist_t activation_record,
                     plist_t handshake_response)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;

    if (!dev || !activation_record || !handshake_response) {
        log_error("%s Invalid arguments to session_activate", LOG_TAG);
        return -1;
    }

    if (ma_session_start(dev, &ma) < 0)
        return -1;

    /*
     * Stage 5: HandleActivationInfoWithSessionRequest.
     *
     * In the online flow, stage 4 would first POST to
     * APPLE_DEVICE_ACTIVATION_URL with:
     *   Content-Type: application/x-www-form-urlencoded
     *   Accept: *\/*
     *   Body: "activation-info=" + URL-encoded activation plist
     * The HTTP response body + headers would then be passed here.
     *
     * In offline mode, the activation_record was constructed locally
     * (by record.c) and accepted by the patched mobileactivationd.
     * This call finalizes activation by submitting the record along
     * with the handshake session context.
     */
    err = mobileactivation_activate_with_session(
            ma, activation_record, handshake_response);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s activate_with_session failed (error %d)",
                  LOG_TAG, (int)err);
        return -1;
    }

    log_info("%s Session activation completed successfully", LOG_TAG);
    return 0;
}
