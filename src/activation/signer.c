/*
 * signer.c -- ECDSA P-256 sign / verify helpers.
 *
 * OpenSSL 3.x only. Signatures are SHA-256 + ECDSA, DER-encoded.
 * Keys are PEM on disk (PKCS#8 private, SPKI public).
 */

#include "activation/signer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "util/log.h"

#define LOG_TAG "[signer]"

/* --- Internal: log OpenSSL error --------------------------------- */

static void log_ssl_error(const char *where)
{
    unsigned long e;
    char          buf[256];

    e = ERR_get_error();
    if (e == 0) {
        log_error("%s %s: (no OpenSSL error queued)", LOG_TAG, where);
        return;
    }
    ERR_error_string_n(e, buf, sizeof(buf));
    log_error("%s %s: %s", LOG_TAG, where, buf);
    /* Drain any remaining errors so they don't leak into later calls. */
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        log_error("%s   also: %s", LOG_TAG, buf);
    }
}

/* --- Internal: PEM key loaders ----------------------------------- */

static EVP_PKEY *load_private_key(const char *pem_path)
{
    FILE     *fp;
    EVP_PKEY *pkey;

    if (!pem_path) return NULL;
    fp = fopen(pem_path, "r");
    if (!fp) {
        log_error("%s Cannot open private key %s", LOG_TAG, pem_path);
        return NULL;
    }
    pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    if (!pkey) {
        log_ssl_error("PEM_read_PrivateKey");
        return NULL;
    }
    return pkey;
}

static EVP_PKEY *load_public_key(const char *pem_path)
{
    FILE     *fp;
    EVP_PKEY *pkey;

    if (!pem_path) return NULL;
    fp = fopen(pem_path, "r");
    if (!fp) {
        log_error("%s Cannot open public key %s", LOG_TAG, pem_path);
        return NULL;
    }
    pkey = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
    fclose(fp);
    if (!pkey) {
        log_ssl_error("PEM_read_PUBKEY");
        return NULL;
    }
    return pkey;
}

/* --- Public: keygen ---------------------------------------------- */

int signer_generate_test_keypair(const char *priv_pem_path,
                                 const char *pub_pem_path)
{
    EVP_PKEY *pkey = NULL;
    FILE     *fp   = NULL;
    int       rc   = -1;

    if (!priv_pem_path || !pub_pem_path) return -1;

    pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    if (!pkey) {
        log_ssl_error("EVP_PKEY_Q_keygen");
        return -1;
    }

    fp = fopen(priv_pem_path, "w");
    if (!fp) {
        log_error("%s Cannot open %s for writing", LOG_TAG, priv_pem_path);
        goto out;
    }
    if (!PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL)) {
        log_ssl_error("PEM_write_PrivateKey");
        fclose(fp);
        goto out;
    }
    fclose(fp);
    fp = NULL;

    fp = fopen(pub_pem_path, "w");
    if (!fp) {
        log_error("%s Cannot open %s for writing", LOG_TAG, pub_pem_path);
        goto out;
    }
    if (!PEM_write_PUBKEY(fp, pkey)) {
        log_ssl_error("PEM_write_PUBKEY");
        fclose(fp);
        goto out;
    }
    fclose(fp);
    fp = NULL;

    rc = 0;

out:
    EVP_PKEY_free(pkey);
    return rc;
}

/* --- Public: sign ------------------------------------------------ */

int signer_ecdsa_sign(const char *pem_path,
                      const unsigned char *data, size_t len,
                      unsigned char **sig, size_t *sig_len)
{
    EVP_PKEY     *pkey = NULL;
    EVP_MD_CTX   *ctx  = NULL;
    unsigned char *out = NULL;
    size_t         out_len = 0;
    int            rc = -1;

    if (!pem_path || !sig || !sig_len) return -1;
    if (len > 0 && !data) return -1;

    *sig     = NULL;
    *sig_len = 0;

    log_debug("%s signer_ecdsa_sign: loading key from %s, data=%zu bytes",
              LOG_TAG, pem_path, len);
    pkey = load_private_key(pem_path);
    if (!pkey) return -1;

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        log_ssl_error("EVP_MD_CTX_new");
        goto out;
    }

    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        log_ssl_error("EVP_DigestSignInit");
        goto out;
    }
    if (len > 0 &&
        EVP_DigestSignUpdate(ctx, data, len) <= 0) {
        log_ssl_error("EVP_DigestSignUpdate");
        goto out;
    }
    /* Probe signature length. */
    if (EVP_DigestSignFinal(ctx, NULL, &out_len) <= 0) {
        log_ssl_error("EVP_DigestSignFinal (probe)");
        goto out;
    }
    out = malloc(out_len ? out_len : 1);
    if (!out) {
        log_error("%s malloc(%zu) failed", LOG_TAG, out_len);
        goto out;
    }
    if (EVP_DigestSignFinal(ctx, out, &out_len) <= 0) {
        log_ssl_error("EVP_DigestSignFinal (final)");
        free(out);
        out = NULL;
        goto out;
    }

    *sig     = out;
    *sig_len = out_len;
    out      = NULL;
    rc       = 0;
    log_debug("%s signer_ecdsa_sign: signature %zu bytes (DER)", LOG_TAG, out_len);

out:
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

/* --- Public: verify ---------------------------------------------- */

int signer_ecdsa_verify(const char *pem_path,
                        const unsigned char *data, size_t len,
                        const unsigned char *sig, size_t sig_len)
{
    EVP_PKEY   *pkey = NULL;
    EVP_MD_CTX *ctx  = NULL;
    int         rc   = -1;
    int         v;

    if (!pem_path || !sig || sig_len == 0) return -1;
    if (len > 0 && !data) return -1;

    log_debug("%s signer_ecdsa_verify: key=%s, data=%zu bytes, sig=%zu bytes",
              LOG_TAG, pem_path, len, sig_len);
    pkey = load_public_key(pem_path);
    if (!pkey) return -1;

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
        log_ssl_error("EVP_MD_CTX_new");
        goto out;
    }

    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        log_ssl_error("EVP_DigestVerifyInit");
        goto out;
    }
    if (len > 0 &&
        EVP_DigestVerifyUpdate(ctx, data, len) <= 0) {
        log_ssl_error("EVP_DigestVerifyUpdate");
        goto out;
    }
    v = EVP_DigestVerifyFinal(ctx, sig, sig_len);
    if (v == 1) {
        rc = 1;
    } else if (v == 0) {
        /* Signature is structurally valid but does not verify.
         * Drain the error queue to avoid polluting later logs. */
        while (ERR_get_error() != 0) { /* nothing */ }
        rc = 0;
    } else {
        log_ssl_error("EVP_DigestVerifyFinal");
        rc = -1;
    }

out:
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}
