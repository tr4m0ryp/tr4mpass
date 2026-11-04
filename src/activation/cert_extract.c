/*
 * cert_extract.c -- Device certificate extraction via lockdownd.
 *
 * Reads X.509 certificate blobs from a live lockdownd client and hands
 * them back to the caller as malloc'd byte buffers. Handles the three
 * possible plist node types lockdownd returns for certificate fields:
 *
 *   - PLIST_DATA   -- raw bytes, copied straight through.
 *   - PLIST_STRING -- base64-encoded; decoded in-place.
 *   - PLIST_ARRAY  -- CertificateChain fallback; first element is used.
 *
 * No emojis anywhere, per project rules.
 */

#include "activation/cert_extract.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <plist/plist.h>

#include "util/log.h"
#include "util/plist_helpers.h"

#define LOG_TAG "[cert_extract]"

#define DOMAIN_LOCKDOWN   "com.apple.mobile.lockdown"
#define DOMAIN_ACTIVATION "com.apple.mobile.activation"

/* --- Internal: base64 decode -------------------------------------- */

/* RFC 4648 base64 alphabet index. 0xFF for ignored/invalid. */
static const unsigned char b64_table[256] = {
    ['A']= 0,['B']= 1,['C']= 2,['D']= 3,['E']= 4,['F']= 5,['G']= 6,['H']= 7,
    ['I']= 8,['J']= 9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,
    ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
    ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
    ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
    ['y']=50,['z']=51,
    ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
    ['8']=60,['9']=61,['+']=62,['/']=63,
};

static int b64_is_valid(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/';
}

/*
 * Decode base64 input of length `in_len`. Returns a malloc'd buffer
 * and writes its length to *out_len. Returns NULL on allocation
 * failure or malformed input.
 */
static unsigned char *b64_decode(const char *in, size_t in_len, size_t *out_len)
{
    size_t         i;
    size_t         valid = 0;
    size_t         padded;
    size_t         cap;
    size_t         o = 0;
    uint32_t       acc = 0;
    int            bits = 0;
    unsigned char *out;

    if (!in || !out_len) return NULL;
    *out_len = 0;

    /* First pass: count valid base64 chars. */
    for (i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (b64_is_valid(c)) valid++;
    }
    if (valid == 0) {
        out = malloc(1);
        if (out) *out = 0;
        *out_len = 0;
        return out;
    }

    padded = (valid + 3) & ~(size_t)3;
    cap    = (padded / 4) * 3;
    out    = malloc(cap ? cap : 1);
    if (!out) return NULL;

    for (i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (!b64_is_valid(c)) continue;
        acc  = (acc << 6) | b64_table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }

    *out_len = o;
    return out;
}

/* --- Internal: plist node -> cert_blob ---------------------------- */

static int blob_from_plist_node(plist_t node, cert_blob_t *out)
{
    plist_type t;

    if (!node || !out) return -1;
    out->data = NULL;
    out->len  = 0;

    t = plist_get_node_type(node);
    log_debug("%s blob_from_plist_node: node type=%d", LOG_TAG, (int)t);

    if (t == PLIST_DATA) {
        char    *buf = NULL;
        uint64_t len = 0;
        plist_get_data_val(node, &buf, &len);
        if (!buf || len == 0) {
            plist_mem_free(buf);
            log_error("%s PLIST_DATA node is empty", LOG_TAG);
            return -1;
        }
        out->data = malloc((size_t)len);
        if (!out->data) {
            plist_mem_free(buf);
            return -1;
        }
        memcpy(out->data, buf, (size_t)len);
        out->len = (size_t)len;
        plist_mem_free(buf);
        return 0;
    }

    if (t == PLIST_STRING) {
        char   *s = NULL;
        size_t  sl;
        size_t  dl;
        plist_get_string_val(node, &s);
        if (!s) {
            log_error("%s PLIST_STRING node unreadable", LOG_TAG);
            return -1;
        }
        sl = strlen(s);
        out->data = b64_decode(s, sl, &dl);
        plist_mem_free(s);
        if (!out->data || dl == 0) {
            free(out->data);
            out->data = NULL;
            log_error("%s base64 decode failed or empty", LOG_TAG);
            return -1;
        }
        out->len = dl;
        return 0;
    }

    if (t == PLIST_ARRAY) {
        plist_t first;
        uint32_t cnt = plist_array_get_size(node);
        if (cnt == 0) {
            log_error("%s PLIST_ARRAY node is empty", LOG_TAG);
            return -1;
        }
        first = plist_array_get_item(node, 0);
        if (!first) {
            log_error("%s PLIST_ARRAY first element NULL", LOG_TAG);
            return -1;
        }
        return blob_from_plist_node(first, out);
    }

    log_error("%s Unsupported plist node type %d", LOG_TAG, (int)t);
    return -1;
}

/* --- Internal: lookup helper -------------------------------------- */

static int lookup_blob(lockdownd_client_t client,
                       const char *domain, const char *key,
                       cert_blob_t *out)
{
    plist_t          node = NULL;
    lockdownd_error_t e;
    int               rc;

    if (!client || !key || !out) return -1;
    out->data = NULL;
    out->len  = 0;

    log_debug("%s lookup_blob: domain=%s key=%s", LOG_TAG,
              domain ? domain : "(null)", key);
    e = lockdownd_get_value(client, domain, key, &node);
    if (e != LOCKDOWN_E_SUCCESS || !node) {
        if (node) plist_free(node);
        return -1;
    }

    rc = blob_from_plist_node(node, out);
    plist_free(node);
    return rc;
}

/* --- Public API --------------------------------------------------- */

void cert_blob_free(cert_blob_t *blob)
{
    if (!blob) return;
    if (blob->data) {
        free(blob->data);
        blob->data = NULL;
    }
    blob->len = 0;
}

int cert_extract_device_certificate(lockdownd_client_t client,
                                    cert_blob_t *out)
{
    int rc;

    if (!out) return -1;
    out->data = NULL;
    out->len  = 0;

    rc = lookup_blob(client, DOMAIN_LOCKDOWN, "DeviceCertificate", out);
    if (rc != 0) {
        log_error("%s DeviceCertificate extraction failed", LOG_TAG);
        return -1;
    }
    log_info("%s Extracted DeviceCertificate (%zu bytes)", LOG_TAG, out->len);
    return 0;
}

int cert_extract_account_token_certificate(lockdownd_client_t client,
                                           cert_blob_t *out)
{
    if (!out) return -1;
    out->data = NULL;
    out->len  = 0;

    if (lookup_blob(client, NULL, "AccountTokenCertificate", out) == 0) {
        log_info("%s Extracted AccountTokenCertificate (%zu bytes)",
                 LOG_TAG, out->len);
        return 0;
    }

    if (lookup_blob(client, NULL, "ActivationStateDeadline", out) == 0) {
        log_info("%s AccountTokenCertificate from ActivationStateDeadline "
                 "(%zu bytes)", LOG_TAG, out->len);
        return 0;
    }

    if (lookup_blob(client, NULL, "CertificateChain", out) == 0) {
        log_info("%s AccountTokenCertificate from CertificateChain[0] "
                 "(%zu bytes)", LOG_TAG, out->len);
        return 0;
    }

    log_error("%s AccountTokenCertificate extraction failed (all sources)",
              LOG_TAG);
    return -1;
}

int cert_extract_unique_device_certificate(lockdownd_client_t client,
                                           cert_blob_t *out)
{
    int rc;

    if (!out) return -1;
    out->data = NULL;
    out->len  = 0;

    rc = lookup_blob(client, DOMAIN_ACTIVATION,
                     "UniqueDeviceCertificate", out);
    if (rc != 0) {
        log_error("%s UniqueDeviceCertificate extraction failed", LOG_TAG);
        return -1;
    }
    log_info("%s Extracted UniqueDeviceCertificate (%zu bytes)",
             LOG_TAG, out->len);
    return 0;
}
