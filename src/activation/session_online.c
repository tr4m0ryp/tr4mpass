/* session_online.c -- Online activation stages 2 and 4 via Albert server. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include "compat/plist_compat.h"

#include "activation/session.h"
#include "device/device.h"
#include "util/log.h"

#define DRM_HANDSHAKE_URL    "https://albert.apple.com/deviceservices/drmHandshake"
#define DEVICE_ACTIV_URL     "https://albert.apple.com/deviceservices/deviceActivation"
#define ACTIVATION_UA        "iOS Device Activator (MobileActivation-592.103.2)"
#define HTTP_TIMEOUT_SECS    30L

struct resp_buf {
    uint8_t *data;
    size_t   len;
};

#define MAX_RESPONSE_BYTES (4U * 1024U * 1024U)

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    struct resp_buf *b = ud;
    size_t add = size * nmemb;
    if (b->len + add > MAX_RESPONSE_BYTES) {
        log_error("[session_online] Response exceeds 4 MB limit, aborting");
        return 0;
    }
    uint8_t *tmp = realloc(b->data, b->len + add + 1);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';
    return add;
}

static int http_post(const char *url,
                     const char *body, size_t body_len,
                     const char *content_type,
                     struct resp_buf *out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist *headers = NULL;
    char ct[256];
    snprintf(ct, sizeof(ct), "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, ct);
    headers = curl_slist_append(headers, "Accept: application/xml");

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_POST,           1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)body_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      ACTIVATION_UA);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        HTTP_TIMEOUT_SECS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode rc = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        log_error("[session_online] curl error: %s", curl_easy_strerror(rc));
        return -1;
    }
    if (http_code != 200) {
        log_error("[session_online] HTTP %ld from %s", http_code, url);
        if (out->data && out->len > 0)
            log_debug("[session_online] Error body: %.*s",
                      (int)(out->len < 512 ? out->len : 512), out->data);
        return -1;
    }
    return 0;
}

int session_drm_handshake_online(device_info_t *dev, plist_t session_info,
                                 plist_t *handshake_response)
{
    char    *xml     = NULL;
    uint32_t xml_len = 0;
    struct resp_buf resp = {NULL, 0};

    (void)dev;

    if (!session_info || !handshake_response) {
        log_error("[session_online] NULL args to drm_handshake_online");
        return -1;
    }
    *handshake_response = NULL;

    /* Albert expects the full session_info XML plist (with CollectionBlob
     * base64-encoded inside it), not just the decoded CollectionBlob bytes.
     * go-ios sends ~44 KB = the ~33 KB CollectionBlob data base64-encoded
     * (~44 KB) plus the surrounding XML plist structure. */
    plist_to_xml(session_info, &xml, &xml_len);
    if (!xml || xml_len == 0) {
        log_error("[session_online] Failed to serialize session_info to XML");
        return -1;
    }

    log_info("[session_online] POST %u bytes -> drmHandshake...", xml_len);

    if (http_post(DRM_HANDSHAKE_URL, xml, (size_t)xml_len,
                  "application/x-apple-plist", &resp) != 0) {
        free(xml);
        free(resp.data);
        return -1;
    }
    free(xml);

    log_info("[session_online] Got %zu bytes from drmHandshake", resp.len);

    tp_plist_from_memory((const char *)resp.data, (uint32_t)resp.len,
                      handshake_response, NULL);
    free(resp.data);

    if (!*handshake_response) {
        log_error("[session_online] Failed to parse drmHandshake response");
        return -1;
    }

    log_info("[session_online] drmHandshake OK (online)");
    return 0;
}

int session_device_activation_online(device_info_t *dev,
                                     plist_t activation_info,
                                     plist_t *activation_record)
{
    char    *xml     = NULL;
    uint32_t xml_len = 0;
    char    *encoded = NULL;
    char    *body    = NULL;
    size_t   body_len;
    struct resp_buf resp = {NULL, 0};
    CURL    *curl_esc;

    (void)dev;

    if (!activation_info || !activation_record) {
        log_error("[session_online] NULL args to device_activation_online");
        return -1;
    }
    *activation_record = NULL;

    plist_to_xml(activation_info, &xml, &xml_len);
    if (!xml || xml_len == 0) {
        log_error("[session_online] Failed to serialize activation_info");
        return -1;
    }

    curl_esc = curl_easy_init();
    if (!curl_esc) { free(xml); return -1; }
    encoded = curl_easy_escape(curl_esc, xml, (int)xml_len);
    curl_easy_cleanup(curl_esc);
    free(xml);

    if (!encoded) {
        log_error("[session_online] URL-encode of activation_info failed");
        return -1;
    }

    body_len = strlen("activation-info=") + strlen(encoded);
    body = malloc(body_len + 1);
    if (!body) { curl_free(encoded); return -1; }
    snprintf(body, body_len + 1, "activation-info=%s", encoded);
    curl_free(encoded);

    log_info("[session_online] POST %zu bytes -> deviceActivation...", body_len);

    if (http_post(DEVICE_ACTIV_URL, body, body_len,
                  "application/x-www-form-urlencoded", &resp) != 0) {
        free(body);
        free(resp.data);
        return -1;
    }
    free(body);

    log_info("[session_online] Got %zu bytes from deviceActivation", resp.len);

    tp_plist_from_memory((const char *)resp.data, (uint32_t)resp.len,
                      activation_record, NULL);
    free(resp.data);

    if (!*activation_record) {
        log_error("[session_online] Failed to parse deviceActivation response");
        return -1;
    }

    log_info("[session_online] deviceActivation OK (online)");
    return 0;
}

/* Probe: dump IngestBody fields + drmHandshake only (no FMiP trigger) */

/*
 * Print every key/value from the IngestBody JSON.  Uses a simple
 * key-scan rather than a full JSON parser to avoid adding a dependency.
 * Masks long opaque blobs to keep the output readable.
 */
static void dump_ingest_body(const char *json, size_t len)
{
    const char *p   = json;
    const char *end = json + len;

    printf("\n--- IngestBody fields (inside ECDSA signature) ---\n");

    while (p < end) {
        /* Find next "key": pattern */
        const char *ks = memchr(p, '"', (size_t)(end - p));
        if (!ks) break;
        ks++;
        const char *ke = memchr(ks, '"', (size_t)(end - ks));
        if (!ke) break;

        size_t klen = (size_t)(ke - ks);
        char key[128] = {0};
        if (klen < sizeof(key))
            memcpy(key, ks, klen);
        else
            memcpy(key, ks, sizeof(key) - 1);

        p = ke + 1;

        /* Skip whitespace and ':' */
        while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) p++;
        if (p >= end) break;

        /* Extract value (string or number) */
        char val[256] = {0};
        if (*p == '"') {
            /* String value */
            const char *vs = p + 1;
            const char *ve = memchr(vs, '"', (size_t)(end - vs));
            if (!ve) break;
            size_t vlen = (size_t)(ve - vs);
            if (vlen > 80) {
                snprintf(val, sizeof(val), "[%zu bytes, first 16: %.16s...]",
                         vlen, vs);
            } else {
                if (vlen < sizeof(val))
                    memcpy(val, vs, vlen);
            }
            p = ve + 1;
        } else {
            /* Non-string: read until delimiter */
            const char *vs = p;
            while (p < end && *p != ',' && *p != '}' && *p != '\n') p++;
            size_t vlen = (size_t)(p - vs);
            if (vlen < sizeof(val))
                memcpy(val, vs, vlen);
        }

        printf("  %-30s : %s\n", key, val);
    }
    printf("---------------------------------------------------\n\n");
}

int session_probe_albert(device_info_t *dev, plist_t session_info)
{
    char    *xml     = NULL;
    uint32_t xml_len = 0;
    struct resp_buf resp = {NULL, 0};
    plist_t  hs_resp = NULL;

    (void)dev;

    if (!session_info) {
        log_error("[probe] NULL session_info");
        return -1;
    }

    /* ---- 1. Decode CollectionBlob and dump IngestBody ---- */
    {
        plist_t cb = plist_dict_get_item(session_info, "CollectionBlob");
        if (!cb) {
            log_error("[probe] No CollectionBlob in session_info");
            goto skip_ingest;
        }
        char    *cb_data = NULL;
        uint64_t cb_len  = 0;
        plist_get_data_val(cb, &cb_data, &cb_len);

        if (cb_data && cb_len > 0) {
            /* CollectionBlob bytes are themselves an XML plist */
            plist_t cb_plist = NULL;
            tp_plist_from_memory(cb_data, (uint32_t)cb_len, &cb_plist, NULL);

            if (cb_plist) {
                plist_t ib = plist_dict_get_item(cb_plist, "IngestBody");
                if (ib) {
                    plist_type t = plist_get_node_type(ib);
                    char     *ib_str = NULL;

                    if (t == PLIST_STRING) {
                        plist_get_string_val(ib, &ib_str);
                    } else if (t == PLIST_DATA) {
                        uint64_t ib_len = 0;
                        plist_get_data_val(ib, &ib_str, &ib_len);
                    }

                    if (ib_str) {
                        dump_ingest_body(ib_str, strlen(ib_str));
                        free(ib_str);
                    } else {
                        log_warn("[probe] IngestBody present but empty");
                    }
                } else {
                    log_warn("[probe] No IngestBody key in CollectionBlob plist");
                    /* Dump CollectionBlob top-level keys anyway */
                    plist_dict_iter it = NULL;
                    plist_dict_new_iter(cb_plist, &it);
                    char *k = NULL; plist_t v = NULL;
                    printf("\n--- CollectionBlob top-level keys ---\n");
                    while (1) {
                        plist_dict_next_item(cb_plist, it, &k, &v);
                        if (!k) break;
                        printf("  %s\n", k);
                        free(k); k = NULL;
                    }
                    free(it);
                    printf("--------------------------------------\n\n");
                }
                plist_free(cb_plist);
            } else {
                log_warn("[probe] CollectionBlob is not a plist (raw %llu bytes)",
                         (unsigned long long)cb_len);
                printf("[probe] First 64 bytes of CollectionBlob: ");
                for (uint64_t i = 0; i < cb_len && i < 64; i++)
                    printf("%02x", (unsigned char)cb_data[i]);
                printf("\n");
            }
        }
        free(cb_data);
    }

skip_ingest:

    /* ---- 2. Dump HRM size for reference ---- */
    {
        plist_t hrm = plist_dict_get_item(session_info, "HandshakeRequestMessage");
        if (hrm) {
            char    *hbuf = NULL;
            uint64_t hlen = 0;
            plist_get_data_val(hrm, &hbuf, &hlen);
            printf("[probe] HandshakeRequestMessage: %llu bytes\n",
                   (unsigned long long)hlen);
            free(hbuf);
        }
    }

    /* ---- 3. POST to drmHandshake (stage 2 only, no deviceActivation) ---- */
    printf("[probe] Sending session_info to Albert drmHandshake...\n");

    plist_to_xml(session_info, &xml, &xml_len);
    if (!xml || xml_len == 0) {
        log_error("[probe] Failed to serialize session_info");
        return -1;
    }
    printf("[probe] Payload size: %u bytes\n", xml_len);

    if (http_post(DRM_HANDSHAKE_URL, xml, (size_t)xml_len,
                  "application/x-apple-plist", &resp) != 0) {
        free(xml);
        free(resp.data);
        printf("[probe] drmHandshake: FAILED (HTTP error)\n");
        return -1;
    }
    free(xml);

    printf("[probe] drmHandshake: HTTP 200 OK -- %zu bytes received\n",
           resp.len);

    /* Parse and dump top-level keys of the handshake response */
    tp_plist_from_memory((const char *)resp.data, (uint32_t)resp.len,
                      &hs_resp, NULL);
    free(resp.data);

    if (hs_resp) {
        printf("[probe] Handshake response top-level keys:\n");
        plist_dict_iter it = NULL;
        plist_dict_new_iter(hs_resp, &it);
        char *k = NULL; plist_t v = NULL;
        while (1) {
            plist_dict_next_item(hs_resp, it, &k, &v);
            if (!k) break;
            plist_type t = plist_get_node_type(v);
            const char *tname = (t == PLIST_DATA)   ? "data"   :
                                (t == PLIST_STRING) ? "string" :
                                (t == PLIST_DICT)   ? "dict"   :
                                (t == PLIST_ARRAY)  ? "array"  :
                                (t == PLIST_UINT)   ? "uint"   : "other";
            printf("  %-30s [%s]\n", k, tname);
            free(k); k = NULL;
        }
        free(it);
        plist_free(hs_resp);
    } else {
        printf("[probe] Could not parse handshake response as plist\n");
        printf("        (response may be binary -- first 32 bytes already logged)\n");
    }

    printf("\n[probe] Stopped before deviceActivation "
           "(FMiP check is at that stage).\n");
    return 0;
}
