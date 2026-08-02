/*
 * mock_libcurl.c -- Link-time interposition of libcurl easy interface.
 *
 * The SUT uses curl for Albert activation server requests.  This mock
 * captures the URL + body passed via curl_easy_setopt, then on
 * curl_easy_perform it invokes the write callback with any staged
 * response body for matching URLs.  CURLINFO_RESPONSE_CODE reports the
 * staged HTTP status.
 */

#include "mock_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <curl/curl.h>

/* Type alias for the write callback signature used by curl. */
typedef size_t (*curl_write_cb_t)(void *ptr, size_t size, size_t nmemb,
                                  void *userdata);

/* ------------------------------------------------------------------ */
/* Curl handle                                                        */
/* ------------------------------------------------------------------ */

struct mock_curl_easy {
    uint32_t         magic;
    char             url[1024];
    curl_write_cb_t  write_cb;
    void            *write_data;
    long             last_http_code;
};

#define CURL_MAGIC 0x43524C45u /* "CRLE" */

/* Note: the public type is `CURL` which on the real libcurl is a void
 * alias.  We redeclare via typedef so our internal struct is legal
 * without conflicting.  The caller never dereferences it. */

/* ------------------------------------------------------------------ */
/* Staging                                                            */
/* ------------------------------------------------------------------ */

#define CURL_STAGE_SLOTS 16

typedef struct {
    int    in_use;
    char   substr[512];
    long   http_code;
    void  *body;
    size_t body_len;
} curl_stage_t;

static curl_stage_t g_curl_stages[CURL_STAGE_SLOTS];
static int          g_perform_count;
static char         g_last_url[1024];

void mock_curl_reset(void)
{
    int i;
    for (i = 0; i < CURL_STAGE_SLOTS; i++) {
        free(g_curl_stages[i].body);
        memset(&g_curl_stages[i], 0, sizeof(g_curl_stages[i]));
    }
    g_perform_count = 0;
    g_last_url[0]   = '\0';
}

void mock_curl_set_response(const char *url_substring, long http_code,
                            const void *body, size_t body_len)
{
    int i;
    if (!url_substring)
        return;
    for (i = 0; i < CURL_STAGE_SLOTS; i++) {
        if (!g_curl_stages[i].in_use) {
            curl_stage_t *s = &g_curl_stages[i];
            snprintf(s->substr, sizeof(s->substr), "%s", url_substring);
            s->http_code = http_code;
            s->body      = mock_memdup(body, body_len);
            s->body_len  = body_len;
            s->in_use    = 1;
            return;
        }
    }
}

int mock_curl_get_perform_count(void)
{
    return g_perform_count;
}

const char *mock_curl_get_last_url(void)
{
    return g_last_url;
}

/* ------------------------------------------------------------------ */
/* curl_easy_*                                                        */
/* ------------------------------------------------------------------ */

CURLcode curl_global_init(long flags)
{
    (void)flags;
    return CURLE_OK;
}

CURLcode curl_global_init_mem(long flags, curl_malloc_callback m,
                              curl_free_callback f,
                              curl_realloc_callback r,
                              curl_strdup_callback s,
                              curl_calloc_callback c)
{
    (void)flags; (void)m; (void)f; (void)r; (void)s; (void)c;
    return CURLE_OK;
}

void curl_global_cleanup(void) {}

CURL *curl_easy_init(void)
{
    struct mock_curl_easy *e;
    mock_log_append("curl_easy_init");
    e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->magic = CURL_MAGIC;
    return (CURL *)e;
}

void curl_easy_cleanup(CURL *handle)
{
    struct mock_curl_easy *e = (struct mock_curl_easy *)handle;
    mock_log_append("curl_easy_cleanup");
    if (!e) return;
    e->magic = 0;
    free(e);
}

/* Modern curl.h wraps these two functions in __extension__ type-checking
 * macros.  Undefine them so our mock function definitions are visible to
 * the linker without the macro interfering. */
#undef curl_easy_setopt
CURLcode curl_easy_setopt(CURL *handle, CURLoption option, ...)
{
    struct mock_curl_easy *e = (struct mock_curl_easy *)handle;
    va_list ap;

    if (!e)
        return CURLE_FAILED_INIT;

    va_start(ap, option);
    switch (option) {
    case CURLOPT_URL: {
        const char *url = va_arg(ap, const char *);
        if (url) {
            snprintf(e->url, sizeof(e->url), "%s", url);
            snprintf(g_last_url, sizeof(g_last_url), "%s", url);
            mock_log_append("curl_easy_setopt(URL=%s)", url);
        }
        break;
    }
    case CURLOPT_WRITEFUNCTION: {
        e->write_cb = va_arg(ap, curl_write_cb_t);
        break;
    }
    case CURLOPT_WRITEDATA: {
        e->write_data = va_arg(ap, void *);
        break;
    }
    default:
        /* All other options are discarded; we don't need them. */
        break;
    }
    va_end(ap);
    return CURLE_OK;
}

static curl_stage_t *find_curl_stage(const char *url)
{
    int i;
    if (!url)
        return NULL;
    for (i = 0; i < CURL_STAGE_SLOTS; i++) {
        if (g_curl_stages[i].in_use &&
            strstr(url, g_curl_stages[i].substr))
            return &g_curl_stages[i];
    }
    return NULL;
}

CURLcode curl_easy_perform(CURL *handle)
{
    struct mock_curl_easy *e = (struct mock_curl_easy *)handle;
    curl_stage_t *stage;

    if (!e)
        return CURLE_FAILED_INIT;

    g_perform_count++;
    mock_log_append("curl_easy_perform(url=%s)", e->url);

    stage = find_curl_stage(e->url);
    if (!stage) {
        /* No staging: respond with empty 200. */
        e->last_http_code = 200;
        return CURLE_OK;
    }
    e->last_http_code = stage->http_code;
    if (e->write_cb && stage->body && stage->body_len > 0) {
        e->write_cb(stage->body, 1, stage->body_len, e->write_data);
    }
    return CURLE_OK;
}

#undef curl_easy_getinfo
CURLcode curl_easy_getinfo(CURL *handle, CURLINFO info, ...)
{
    struct mock_curl_easy *e = (struct mock_curl_easy *)handle;
    va_list ap;
    va_start(ap, info);
    if (e && info == CURLINFO_RESPONSE_CODE) {
        long *p = va_arg(ap, long *);
        if (p) *p = e->last_http_code;
    }
    va_end(ap);
    return CURLE_OK;
}

const char *curl_easy_strerror(CURLcode code)
{
    switch (code) {
    case CURLE_OK:              return "No error";
    case CURLE_FAILED_INIT:     return "Failed initialization";
    default:                    return "Mock curl error";
    }
}

char *curl_easy_escape(CURL *handle, const char *string, int length)
{
    size_t n;
    char *buf;
    (void)handle;
    if (!string)
        return NULL;
    n = length > 0 ? (size_t)length : strlen(string);
    buf = malloc(n * 3 + 1);   /* worst case: %HH */
    if (!buf) return NULL;

    /* Trivial percent-encoding of non-alphanumerics.  Adequate for tests. */
    {
        size_t i, o = 0;
        static const char hex[] = "0123456789ABCDEF";
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)string[i];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                buf[o++] = (char)c;
            } else {
                buf[o++] = '%';
                buf[o++] = hex[c >> 4];
                buf[o++] = hex[c & 0xF];
            }
        }
        buf[o] = '\0';
    }
    return buf;
}

void curl_free(void *p) { free(p); }

/* ------------------------------------------------------------------ */
/* curl_slist_*                                                       */
/* ------------------------------------------------------------------ */

struct curl_slist *curl_slist_append(struct curl_slist *list,
                                     const char *value)
{
    struct curl_slist *node = calloc(1, sizeof(*node));
    if (!node) return list;
    node->data = mock_strdup_safe(value);
    node->next = NULL;

    if (!list) return node;

    {
        struct curl_slist *p = list;
        while (p->next) p = p->next;
        p->next = node;
    }
    return list;
}

void curl_slist_free_all(struct curl_slist *list)
{
    while (list) {
        struct curl_slist *next = list->next;
        free(list->data);
        free(list);
        list = next;
    }
}
