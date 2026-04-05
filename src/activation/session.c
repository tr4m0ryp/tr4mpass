/*
 * session.c -- drmHandshake session activation protocol.
 *
 * Implements the session-mode activation flow required since iOS 10.2+.
 * Wraps libimobiledevice mobileactivation session APIs and provides
 * local drmHandshake construction for offline bypass.
 */

#include <stdlib.h>
#include <string.h>

#include <libimobiledevice/mobileactivation.h>
#include "activation/session.h"
#include "activation/activation.h"
#include "util/log.h"
#include "util/plist_helpers.h"

#define LOG_TAG "[session]"

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

    err = mobileactivation_client_start_service(dev->handle, ma, "itr4m");
    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s Failed to start mobileactivation service (error %d)",
                  LOG_TAG, (int)err);
        return -1;
    }

    return 0;
}

/*
 * Build a minimal handshake response dict for offline mode.
 * In a real server-assisted flow, this would come from
 * POST https://albert.apple.com/deviceservices/drmHandshake.
 * For offline bypass, we construct the minimum required fields.
 */
static plist_t build_offline_handshake(device_info_t *dev,
                                       plist_t session_info)
{
    plist_t response = NULL;
    plist_t fdr_blob = NULL;
    plist_t su_info = NULL;

    if (!dev || !session_info) return NULL;

    response = plist_new_dict();
    if (!response) return NULL;

    /*
     * TODO: Generate proper FDRBlob from device FairPlay data.
     * For now, use an empty data blob as placeholder.
     */
    fdr_blob = plist_new_data("", 0);
    plist_dict_set_item(response, "FDRBlob", fdr_blob);

    /* TODO: Populate SUInfo from device session blob */
    su_info = plist_new_dict();
    plist_dict_set_item(response, "SUInfo", su_info);

    /* TODO: Generate HandshakeResponseMessage from session request */
    plist_dict_set_item(response, "HandshakeResponseMessage",
                        plist_new_data("", 0));

    /* TODO: Generate serverKP key pair for session encryption */
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
     * In a server-assisted flow, we would POST the session_info to
     * https://albert.apple.com/deviceservices/drmHandshake and receive
     * the handshake response. For offline bypass, we construct the
     * response locally.
     *
     * The session_info contains:
     *   - CollectionBlob: FairPlay DRM collection data
     *   - HandshakeRequestMessage: device-side handshake initiation
     *   - UniqueDeviceID: device UDID
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
     * CreateTunnel1ActivationInfoRequest: takes the handshake response
     * (FDRBlob, SUInfo, HandshakeResponseMessage, serverKP) and produces
     * the final activation info blob incorporating session state.
     */
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
     * HandleActivationInfoWithSessionRequest: finalizes activation
     * by submitting the activation record along with the handshake
     * session context. This is the last step of the drmHandshake
     * protocol.
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
