/*
 * env_config.c -- Central env-var lookup and summary for tr4mpass.
 *
 * Every bypass component that reads TR4MPASS_* configuration goes
 * through env_get / env_get_int here so defaulting and logging stay
 * consistent.  The list of variables printed by --verbose lives in
 * `g_tr4mpass_vars` below; adding a new TR4MPASS_* means adding a row.
 */

#include "util/env_config.h"
#include "util/log.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Basic lookups                                                      */
/* ------------------------------------------------------------------ */

const char *env_get(const char *name, const char *default_value)
{
    if (!name || !*name)
        return default_value;
    const char *v = getenv(name);
    if (v == NULL)
        return default_value;
    return v;
}

int env_get_int(const char *name, int default_value)
{
    const char *v = env_get(name, NULL);
    if (v == NULL || *v == '\0')
        return default_value;

    errno = 0;
    char *end = NULL;
    long parsed = strtol(v, &end, 10);
    if (errno != 0 || end == v || (end && *end != '\0'))
        return default_value;
    if (parsed < INT_MIN || parsed > INT_MAX)
        return default_value;
    return (int)parsed;
}

int env_require_all(const char *const *names, size_t count)
{
    if (!names || count == 0)
        return 0;

    size_t missing_count = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *n = names[i];
        if (!n || !*n) {
            missing_count++;
            continue;
        }
        const char *v = getenv(n);
        if (v == NULL || *v == '\0')
            missing_count++;
    }

    if (missing_count == 0)
        return 0;

    log_warn("env_require_all: %zu of %zu required variable(s) missing:",
             missing_count, count);
    for (size_t i = 0; i < count; ++i) {
        const char *n = names[i];
        if (!n || !*n) {
            log_warn("  (null name at index %zu)", i);
            continue;
        }
        const char *v = getenv(n);
        if (v == NULL || *v == '\0')
            log_warn("  %s", n);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Summary printer                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    ENV_KIND_PATH,    /* print full value */
    ENV_KIND_SECRET,  /* abbreviate value */
    ENV_KIND_PLAIN    /* short value, print in full */
} env_kind_t;

typedef struct {
    const char *name;
    env_kind_t  kind;
} env_entry_t;

static const env_entry_t g_tr4mpass_vars[] = {
    { TR4MPASS_ENV_RAMDISK_PATH,    ENV_KIND_PATH   },
    { TR4MPASS_ENV_IBSS_PATH,       ENV_KIND_PATH   },
    { TR4MPASS_ENV_IPSW_PATH,       ENV_KIND_PATH   },
    { TR4MPASS_ENV_IBEC_PATH,       ENV_KIND_PATH   },
    { TR4MPASS_ENV_DEVICETREE_PATH, ENV_KIND_PATH   },
    { TR4MPASS_ENV_TRUSTCACHE_PATH, ENV_KIND_PATH   },
    { TR4MPASS_ENV_PATCHED_MAD,     ENV_KIND_PATH   },
    { TR4MPASS_ENV_SSH_HOST,        ENV_KIND_PLAIN  },
    { TR4MPASS_ENV_SSH_PORT,        ENV_KIND_PLAIN  },
    { TR4MPASS_ENV_SSH_USER,        ENV_KIND_PLAIN  },
    { TR4MPASS_ENV_SSH_PASSWORD,    ENV_KIND_SECRET },
    { TR4MPASS_ENV_SIGNING_KEY,     ENV_KIND_SECRET },
    { TR4MPASS_ENV_RAMDISK_TIMEOUT, ENV_KIND_PLAIN  },
};

static size_t g_tr4mpass_vars_count =
    sizeof(g_tr4mpass_vars) / sizeof(g_tr4mpass_vars[0]);

static void print_secret(const char *value)
{
    if (value == NULL) {
        printf("<unset>");
        return;
    }
    size_t n = strlen(value);
    if (n == 0) {
        printf("<empty>");
        return;
    }
    char head[9] = {0};
    size_t copy = n < 8 ? n : 8;
    memcpy(head, value, copy);
    printf("SET (%s%s, %zu chars)",
           head, (n > 8 ? "..." : ""), n);
}

static void print_one(const env_entry_t *entry)
{
    const char *v = getenv(entry->name);
    printf("  %-30s ", entry->name);

    if (v == NULL) {
        printf("<unset>\n");
        return;
    }

    switch (entry->kind) {
    case ENV_KIND_SECRET:
        print_secret(v);
        printf("\n");
        break;
    case ENV_KIND_PATH:
    case ENV_KIND_PLAIN:
    default:
        if (*v == '\0')
            printf("<empty>\n");
        else
            printf("%s\n", v);
        break;
    }
}

void env_print_tr4mpass_summary(void)
{
    printf("\n"
           "----------------------------------------\n"
           "  TR4MPASS environment configuration\n"
           "----------------------------------------\n");
    for (size_t i = 0; i < g_tr4mpass_vars_count; ++i)
        print_one(&g_tr4mpass_vars[i]);
    printf("----------------------------------------\n\n");
}
