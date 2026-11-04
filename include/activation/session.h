/*
 * session.h -- drmHandshake session activation protocol.
 *
 * Wraps libimobiledevice's session-mode mobileactivation API for the
 * drmHandshake protocol required since iOS 10.2+.
 *
 * Protocol stages (from go-ios):
 *   1. CreateTunnel1SessionInfoRequest   -> HandshakeRequestMessage blob
 *   2. drmHandshake (offline or online)  -> handshake response
 *   3. CreateActivationInfoRequest       -> activation info plist
 *   4. deviceActivation (offline/online) -> activation record
 *   5. HandleActivationInfoWithSessionRequest -> finalize
 *
 * Offline mode: stages 2 and 4 construct responses locally (requires
 * patched mobileactivationd -- Path A delivery only).
 * Online mode: stages 2 and 4 contact albert.apple.com directly.
 */

#ifndef SESSION_H
#define SESSION_H

#include <plist/plist.h>
#include "device/device.h"

/*
 * session_get_info -- Retrieve session info blob from device.
 * Sends CreateTunnel1SessionInfoRequest via mobileactivation service.
 * On success, *session_info is set to a new plist that the caller must
 * free with plist_free().
 * Returns 0 on success, -1 on error.
 */
int session_get_info(device_info_t *dev, plist_t *session_info);

/*
 * session_drm_handshake -- Perform local drmHandshake using session info.
 * In offline mode, constructs the handshake response locally from the
 * session blob (CollectionBlob, HandshakeRequestMessage, UniqueDeviceID).
 * On success, *handshake_response is set to a new plist dict containing
 * FDRBlob, SUInfo, HandshakeResponseMessage, and serverKP.
 * Caller must free with plist_free().
 * Returns 0 on success, -1 on error.
 */
int session_drm_handshake(device_info_t *dev, plist_t session_info,
                          plist_t *handshake_response);

/*
 * session_create_activation_info -- Create activation info with session.
 * Sends CreateActivationInfoRequest with BasebandWaitCount=90 and the
 * handshake response to produce the activation info blob.
 * On success, *activation_info is set to a new plist. Caller frees.
 * Returns 0 on success, -1 on error.
 */
int session_create_activation_info(device_info_t *dev,
                                   plist_t handshake_response,
                                   plist_t *activation_info);

/*
 * session_activate -- Finalize session-mode activation.
 * Sends HandleActivationInfoWithSessionRequest with the activation record
 * and handshake response. This completes the drmHandshake protocol.
 * Returns 0 on success, -1 on error.
 */
int session_activate(device_info_t *dev, plist_t activation_record,
                     plist_t handshake_response);

/*
 * session_drm_handshake_online -- Online stage 2: POST to Albert.
 * Serializes session_info to binary plist and POSTs to
 * albert.apple.com/deviceservices/drmHandshake. Parses the response
 * with libplist (handles both XML and binary formats).
 * On success, *handshake_response is a new plist. Caller frees.
 * Returns 0 on success, -1 on error.
 */
int session_drm_handshake_online(device_info_t *dev, plist_t session_info,
                                 plist_t *handshake_response);

/*
 * session_device_activation_online -- Online stage 4: POST to Albert.
 * URL-encodes activation_info as XML plist and POSTs to
 * albert.apple.com/deviceservices/deviceActivation.
 * On success, *activation_record is a new plist. Caller frees.
 * Returns 0 on success, -1 on error.
 */
int session_device_activation_online(device_info_t *dev,
                                     plist_t activation_info,
                                     plist_t *activation_record);

/*
 * session_probe_albert -- Diagnostic probe: dump CollectionBlob IngestBody
 * fields and run only the drmHandshake stage against Albert.
 *
 * Prints every key found in the signed IngestBody JSON so the caller can
 * confirm which fields (including iOS version) are inside vs. outside the
 * ECDSA signature boundary.  Then POSTs to drmHandshake and reports the
 * HTTP status and top-level keys of the response — without proceeding to
 * deviceActivation (which would trigger FMiP rejection on locked devices).
 *
 * Returns 0 on success (drmHandshake HTTP 200), -1 on error.
 */
int session_probe_albert(device_info_t *dev, plist_t session_info);

#endif /* SESSION_H */
