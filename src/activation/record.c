/*
 * record.c -- Activation record plist builder.
 *
 * Constructs the activation record plists that the device needs to
 * transition to "Activated" state. Cert fields are pulled live via
 * lockdownd when a client is provided; otherwise fall back to empty
 * placeholders. SEP-bound fields (FairPlayKeyData, RKCData, SepNonce)
 * are unavailable without an SEP compromise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "activation/cert_extract.h"
#include "activation/record.h"
#include "activation/signer.h"
#include "util/log.h"
#include "util/plist_helpers.h"

#define LOG_TAG "[record]"

typedef int (*cert_extract_fn_t)(lockdownd_client_t, cert_blob_t *);

/*
 * set_cert_field -- pull one cert from lockdownd into `record[key]`,
 * falling back to an empty-blob placeholder on NULL client or failure.
 */
static void set_cert_field(plist_t record, lockdownd_client_t client,
                           const char *key, cert_extract_fn_t fn)
{
    cert_blob_t blob = {NULL, 0};

    if (client && fn && fn(client, &blob) == 0 &&
        blob.data && blob.len > 0) {
        log_debug("%s set_cert_field: %s = %zu bytes (live)", LOG_TAG, key, blob.len);
        plist_dict_set_item(record, key,
            plist_new_data((const char *)blob.data, (uint64_t)blob.len));
        cert_blob_free(&blob);
        return;
    }
    if (client)
        log_warn("%s %s extraction failed, using empty placeholder",
                 LOG_TAG, key);
    cert_blob_free(&blob);
    plist_dict_set_item(record, key, plist_new_data("", 0));
}

/*
 * set_account_token_signature -- ECDSA(SHA-256) sign the XML token
 * using $TR4MPASS_SIGNING_KEY. Empty placeholder on env-unset or error.
 */
static void set_account_token_signature(plist_t record, plist_t token)
{
    const char    *key_path = getenv("TR4MPASS_SIGNING_KEY");
    unsigned char *sig      = NULL;
    size_t         sig_len  = 0;
    char          *xml      = NULL;
    uint32_t       xml_len  = 0;

    if (!key_path || !*key_path) {
        log_warn("%s TR4MPASS_SIGNING_KEY unset, "
                 "AccountTokenSignature left empty", LOG_TAG);
        goto empty;
    }
    if (token) plist_to_xml(token, &xml, &xml_len);
    if (!xml || xml_len == 0) {
        log_warn("%s AccountToken serialize failed", LOG_TAG);
        goto empty;
    }
    if (signer_ecdsa_sign(key_path, (const unsigned char *)xml, xml_len,
                          &sig, &sig_len) != 0 || !sig || sig_len == 0) {
        log_warn("%s signer_ecdsa_sign failed", LOG_TAG);
        free(sig);
        goto empty;
    }
    plist_dict_set_item(record, "AccountTokenSignature",
        plist_new_data((const char *)sig, (uint64_t)sig_len));
    free(sig);
    if (xml) plist_mem_free(xml);
    return;

empty:
    if (xml) plist_mem_free(xml);
    plist_dict_set_item(record, "AccountTokenSignature",
                        plist_new_data("", 0));
}

/*
 * build_account_token -- minimal XML AccountToken blob for offline use.
 */
static plist_t build_account_token(const device_info_t *dev)
{
    char token_buf[2048];
    int len;

    len = snprintf(token_buf, sizeof(token_buf),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>InStoreActivation</key>\n"
        "  <false/>\n"
        "  <key>IMEI</key>\n"
        "  <string>%s</string>\n"
        "  <key>MEID</key>\n"
        "  <string>%s</string>\n"
        "  <key>SerialNumber</key>\n"
        "  <string>%s</string>\n"
        "  <key>UniqueDeviceID</key>\n"
        "  <string>%s</string>\n"
        "  <key>ProductType</key>\n"
        "  <string>%s</string>\n"
        "</dict>\n"
        "</plist>",
        dev->imei, dev->meid, dev->serial,
        dev->udid, dev->product_type);

    if (len < 0 || (size_t)len >= sizeof(token_buf)) {
        log_error("%s AccountToken buffer overflow", LOG_TAG);
        return NULL;
    }
    return plist_new_data(token_buf, (uint64_t)len);
}

/*
 * populate_common_fields -- shared between record_build and _a12.
 * When `client` is non-NULL, cert fields are pulled live; otherwise
 * empty placeholders are used. Returns 0 on success, -1 on error.
 */
static int populate_common_fields(plist_t record,
                                  const device_info_t *dev,
                                  lockdownd_client_t client)
{
    plist_t token = NULL;

    if (!record || !dev) return -1;

    log_debug("%s populate_common_fields: building for UDID %s", LOG_TAG, dev->udid);
    token = build_account_token(dev);
    if (!token) {
        log_error("%s Failed to build AccountToken", LOG_TAG);
        return -1;
    }
    plist_dict_set_item(record, "AccountToken", token);
    log_debug("%s populate_common_fields: AccountToken set", LOG_TAG);

    /* AccountTokenCertificate: X.509 cert endorsing the AccountToken. */
    set_cert_field(record, client, "AccountTokenCertificate",
                   cert_extract_account_token_certificate);

    /* AccountTokenSignature: ECDSA signature over the XML token. */
    set_account_token_signature(record, token);

    /* DeviceCertificate: Apple-issued device identity cert. */
    set_cert_field(record, client, "DeviceCertificate",
                   cert_extract_device_certificate);

    /*
     * FairPlayKeyData -- BLOCKED: requires SEP-level access.
     * Sealed by the Secure Enclave; unreadable without an SEP
     * compromise. See 26.3-vulnerability.md. Placeholder empty blob.
     */
    plist_dict_set_item(record, "FairPlayKeyData", plist_new_data("", 0));

    /* UniqueDeviceCertificate: device-bound activation cert. */
    set_cert_field(record, client, "UniqueDeviceCertificate",
                   cert_extract_unique_device_certificate);

    plist_dict_set_item(record, "ack-received", plist_new_bool(1));
    plist_dict_set_item(record, "unbrick",      plist_new_bool(1));
    return 0;
}

/* --- Public API --- */

plist_t record_build_with_client(const device_info_t *dev,
                                 lockdownd_client_t client)
{
    plist_t record;

    if (!dev) { log_error("%s NULL device info", LOG_TAG); return NULL; }
    record = plist_new_dict();
    if (!record) { log_error("%s plist_new_dict failed", LOG_TAG); return NULL; }
    if (populate_common_fields(record, dev, client) < 0) {
        plist_free(record); return NULL;
    }
    log_info("%s Built standard activation record for %s (%s)%s",
             LOG_TAG, dev->udid, dev->product_type,
             client ? " [live certs]" : "");
    return record;
}

plist_t record_build(const device_info_t *dev)
{
    return record_build_with_client(dev, NULL);
}

plist_t record_build_a12_with_client(const device_info_t *dev,
                                     lockdownd_client_t client)
{
    plist_t record;
    char    ecid_str[32];

    if (!dev) { log_error("%s NULL device info", LOG_TAG); return NULL; }
    record = plist_new_dict();
    if (!record) { log_error("%s plist_new_dict failed", LOG_TAG); return NULL; }
    if (populate_common_fields(record, dev, client) < 0) {
        plist_free(record); return NULL;
    }

    /* A12+ SEP-aware fields for the drmHandshake/FActivation path. */
    snprintf(ecid_str, sizeof(ecid_str), "%llu",
             (unsigned long long)dev->ecid);
    plist_dict_set_item(record, "ECID", plist_new_string(ecid_str));
    plist_dict_set_item(record, "HardwareModel",
                        plist_new_string(dev->hardware_model));
    plist_dict_set_item(record, "ChipID", plist_new_uint(dev->cpid));

    /*
     * RKCData -- BLOCKED: requires SEP-level access.
     * Remote Key Chain blob sealed by the Secure Enclave. See
     * 26.3-vulnerability.md. Placeholder empty blob.
     */
    plist_dict_set_item(record, "RKCData", plist_new_data("", 0));

    /*
     * SepNonce -- BLOCKED: requires SEP-level access.
     * Fresh nonce for replay protection, not exposed outside SEP.
     * See 26.3-vulnerability.md. Placeholder empty blob.
     */
    plist_dict_set_item(record, "SepNonce", plist_new_data("", 0));
    plist_dict_set_item(record, "SessionMode", plist_new_bool(1));

    log_info("%s Built A12+ activation record for %s (ECID %s, chip 0x%04x)%s",
             LOG_TAG, dev->udid, ecid_str, dev->cpid,
             client ? " [live certs]" : "");
    return record;
}

plist_t record_build_a12(const device_info_t *dev)
{
    return record_build_a12_with_client(dev, NULL);
}

char *record_to_xml(plist_t record, uint32_t *len)
{
    char *xml = NULL;
    if (!record || !len) {
        log_error("%s Invalid arguments to record_to_xml", LOG_TAG);
        return NULL;
    }
    plist_to_xml(record, &xml, len);
    if (!xml || *len == 0) {
        log_error("%s plist_to_xml failed", LOG_TAG);
        return NULL;
    }
    return xml;
}

void record_free(plist_t record)
{
    if (record) plist_free(record);
}
