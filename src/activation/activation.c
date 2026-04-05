/*
 * activation.c -- Activation protocol client.
 *
 * Wraps the libimobiledevice mobileactivation service for querying
 * activation state, retrieving session blobs, submitting records,
 * session-mode activation (iOS 11.2+), and deactivation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "activation/activation.h"
#include "util/log.h"

#define LOG_TAG "[activation]"

/* --- Internal helpers --- */

/* Start the mobileactivation service and return a client handle. */
static int ma_client_start(device_info_t *dev, mobileactivation_client_t *ma)
{
    mobileactivation_error_t err;

    err = mobileactivation_client_start_service(dev->handle, ma,
                                                 "itr4m");
    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s Could not start mobileactivation service "
                  "(error %d)", LOG_TAG, (int)err);
        return -1;
    }
    return 0;
}

/* Convert a plist node to an XML string. Caller must free *xml. */
static int plist_node_to_xml(plist_t node, char **xml, uint32_t *xml_len)
{
    if (!node || !xml || !xml_len) return -1;

    plist_to_xml(node, xml, xml_len);
    if (!*xml || *xml_len == 0) {
        log_error("%s plist_to_xml failed", LOG_TAG);
        return -1;
    }
    return 0;
}

/* --- Query functions --- */

int activation_get_state(device_info_t *dev, char *buf, size_t buf_len)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    plist_t state = NULL;

    if (!dev || !buf || buf_len == 0) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    buf[0] = '\0';

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    err = mobileactivation_get_activation_state(ma, &state);
    if (err != MOBILEACTIVATION_E_SUCCESS || !state) {
        log_error("%s get_activation_state failed (error %d)",
                  LOG_TAG, (int)err);
        mobileactivation_client_free(ma);
        return -1;
    }

    /* The state plist is typically a string node */
    if (plist_get_node_type(state) == PLIST_STRING) {
        char *str = NULL;
        plist_get_string_val(state, &str);
        if (str) {
            snprintf(buf, buf_len, "%s", str);
            free(str);
        }
    } else {
        /* Fallback: convert the whole plist to XML for inspection */
        char *xml = NULL;
        uint32_t xml_len = 0;
        plist_to_xml(state, &xml, &xml_len);
        if (xml) {
            snprintf(buf, buf_len, "(plist) %.*s",
                     (int)(buf_len - 10 < xml_len ? buf_len - 10 : xml_len),
                     xml);
            plist_mem_free(xml);
        }
    }

    plist_free(state);
    mobileactivation_client_free(ma);
    return 0;
}

int activation_get_session_info(device_info_t *dev,
                                char **blob_xml, uint32_t *blob_len)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    plist_t blob = NULL;

    if (!dev || !blob_xml || !blob_len) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    *blob_xml = NULL;
    *blob_len = 0;

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    err = mobileactivation_create_activation_session_info(ma, &blob);
    if (err != MOBILEACTIVATION_E_SUCCESS || !blob) {
        log_error("%s create_activation_session_info failed (error %d)",
                  LOG_TAG, (int)err);
        mobileactivation_client_free(ma);
        return -1;
    }

    if (plist_node_to_xml(blob, blob_xml, blob_len) < 0) {
        plist_free(blob);
        mobileactivation_client_free(ma);
        return -1;
    }

    log_info("%s Session info blob: %u bytes", LOG_TAG, *blob_len);

    plist_free(blob);
    mobileactivation_client_free(ma);
    return 0;
}

int activation_get_info(device_info_t *dev,
                        char **info_xml, uint32_t *info_len)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    plist_t info = NULL;

    if (!dev || !info_xml || !info_len) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    *info_xml = NULL;
    *info_len = 0;

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    err = mobileactivation_create_activation_info(ma, &info);
    if (err != MOBILEACTIVATION_E_SUCCESS || !info) {
        log_error("%s create_activation_info failed (error %d)",
                  LOG_TAG, (int)err);
        mobileactivation_client_free(ma);
        return -1;
    }

    if (plist_node_to_xml(info, info_xml, info_len) < 0) {
        plist_free(info);
        mobileactivation_client_free(ma);
        return -1;
    }

    log_info("%s Activation info: %u bytes", LOG_TAG, *info_len);

    plist_free(info);
    mobileactivation_client_free(ma);
    return 0;
}

/* --- Submit functions --- */

int activation_submit_record(device_info_t *dev,
                             const char *record_xml, uint32_t record_len)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    plist_t record = NULL;

    if (!dev || !record_xml || record_len == 0) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    plist_from_xml(record_xml, record_len, &record);
    if (!record) {
        log_error("%s Failed to parse activation record XML", LOG_TAG);
        mobileactivation_client_free(ma);
        return -1;
    }

    err = mobileactivation_activate(ma, record);
    plist_free(record);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s activate failed (error %d)", LOG_TAG, (int)err);
        return -1;
    }

    log_info("%s Activation record submitted successfully", LOG_TAG);
    return 0;
}

int activation_submit_record_with_session(device_info_t *dev,
    const char *record_xml, uint32_t record_len, plist_t session_headers)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    plist_t record = NULL;

    if (!dev || !record_xml || record_len == 0 || !session_headers) {
        log_error("%s Invalid arguments for session activation", LOG_TAG);
        return -1;
    }

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    plist_from_xml(record_xml, record_len, &record);
    if (!record) {
        log_error("%s Failed to parse activation record XML", LOG_TAG);
        mobileactivation_client_free(ma);
        return -1;
    }

    err = mobileactivation_activate_with_session(ma, record, session_headers);
    plist_free(record);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s activate_with_session failed (error %d)",
                  LOG_TAG, (int)err);
        return -1;
    }

    log_info("%s Session activation record submitted successfully", LOG_TAG);
    return 0;
}

/* --- State management --- */

int activation_deactivate(device_info_t *dev)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;

    if (!dev) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    if (ma_client_start(dev, &ma) < 0)
        return -1;

    err = mobileactivation_deactivate(ma);
    mobileactivation_client_free(ma);

    if (err != MOBILEACTIVATION_E_SUCCESS) {
        log_error("%s deactivate failed (error %d)", LOG_TAG, (int)err);
        return -1;
    }

    log_info("%s Device deactivated", LOG_TAG);
    return 0;
}

int activation_is_activated(device_info_t *dev)
{
    char state[DEVICE_FIELD_LEN];

    if (!dev) {
        log_error("%s Invalid arguments", LOG_TAG);
        return -1;
    }

    if (activation_get_state(dev, state, sizeof(state)) < 0)
        return -1;

    if (strcmp(state, "Activated") == 0)
        return 1;

    return 0;
}
