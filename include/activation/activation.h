/*
 * activation.h -- Activation protocol client.
 *
 * Wraps the libimobiledevice mobileactivation API for querying activation
 * state, retrieving session info blobs, submitting activation records,
 * and session-mode activation (iOS 11.2+).
 */

#ifndef ACTIVATION_H
#define ACTIVATION_H

#include <stdint.h>
#include <stddef.h>
#include <plist/plist.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/mobileactivation.h>
#include "device/device.h"

/*
 * activation_get_state -- Query the device's current activation state.
 * Writes the state string (e.g. "Unactivated", "Activated") into buf.
 * Returns 0 on success, -1 on error.
 */
int activation_get_state(device_info_t *dev, char *buf, size_t buf_len);

/*
 * activation_get_session_info -- Retrieve the DRM handshake session blob.
 * On success, *blob_xml is set to an XML string and *blob_len to its length.
 * The string is allocated by libplist; caller must free with plist_mem_free(),
 * NOT free() or plist_free().
 * Returns 0 on success, -1 on error.
 */
int activation_get_session_info(device_info_t *dev,
                                char **blob_xml, uint32_t *blob_len);

/*
 * activation_get_info -- Retrieve full activation info from the device.
 * On success, *info_xml is set to an XML string and *info_len to its length.
 * The string is allocated by libplist; caller must free with plist_mem_free(),
 * NOT free() or plist_free().
 * Returns 0 on success, -1 on error.
 */
int activation_get_info(device_info_t *dev,
                        char **info_xml, uint32_t *info_len);

/*
 * activation_submit_record -- Submit an activation record plist to the device.
 * record_xml is an XML plist string. Returns 0 on success, -1 on error.
 */
int activation_submit_record(device_info_t *dev,
                             const char *record_xml, uint32_t record_len);

/*
 * activation_submit_record_with_session -- Submit an activation record with
 * session headers from the drmHandshake response (iOS 11.2+ requirement).
 * session_headers is the plist dict returned by the activation server.
 * Returns 0 on success, -1 on error.
 */
int activation_submit_record_with_session(device_info_t *dev,
    const char *record_xml, uint32_t record_len, plist_t session_headers);

/*
 * activation_deactivate -- Deactivate the device (reset activation state).
 * Returns 0 on success, -1 on error.
 */
int activation_deactivate(device_info_t *dev);

/*
 * activation_is_activated -- Check if the device is currently activated.
 * Returns 1 if activated, 0 if not activated, -1 on error.
 */
int activation_is_activated(device_info_t *dev);

#endif /* ACTIVATION_H */
