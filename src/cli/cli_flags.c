/*
 * cli_flags.c -- Convert bypass-pipeline CLI flags into env vars.
 *
 * We do our own argv walk rather than getopt_long because main.c's
 * getopt_long pass has already permuted argv by the time we run, and
 * the bypass flags (--ramdisk, --ssh-host, ...) would otherwise be
 * separated from their values.  A hand-rolled walk is also the only
 * way to ignore every flag we do not know without coupling to the
 * short-option spec owned by main.c.
 */

#include "cli/cli_flags.h"
#include "util/env_config.h"
#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------- */
/* Flag table                                                     */
/* -------------------------------------------------------------- */

typedef struct {
    const char *long_name;   /* without leading "--" */
    const char *env_name;    /* TR4MPASS_* */
    const char *metavar;     /* for --help: <path>, <host>, ... */
    const char *description; /* human-readable, <= 58 chars */
} flag_map_t;

static const flag_map_t g_flag_map[] = {
    { "ramdisk",      TR4MPASS_ENV_RAMDISK_PATH,
      "<path>", "Staged ramdisk .dmg"                },
    { "ibss",         TR4MPASS_ENV_IBSS_PATH,
      "<path>", "Signed iBSS image"                  },
    { "ipsw",         TR4MPASS_ENV_IPSW_PATH,
      "<path>", "Signed IPSW firmware archive"       },
    { "ibec",         TR4MPASS_ENV_IBEC_PATH,
      "<path>", "Signed iBEC image"                  },
    { "devicetree",   TR4MPASS_ENV_DEVICETREE_PATH,
      "<path>", "DeviceTree image"                   },
    { "trustcache",   TR4MPASS_ENV_TRUSTCACHE_PATH,
      "<path>", "Static trust cache blob"            },
    { "patched-mad",  TR4MPASS_ENV_PATCHED_MAD,
      "<path>", "Patched mobileactivationd binary"   },
    { "ssh-host",     TR4MPASS_ENV_SSH_HOST,
      "<host>", "Ramdisk SSH host (default 127.0.0.1)"},
    { "ssh-port",     TR4MPASS_ENV_SSH_PORT,
      "<port>", "Ramdisk SSH port"                   },
    { "ssh-user",     TR4MPASS_ENV_SSH_USER,
      "<user>", "Ramdisk SSH user (default root)"    },
    { "ssh-password", TR4MPASS_ENV_SSH_PASSWORD,
      "<pass>", "Ramdisk SSH password"               },
    { "signing-key",  TR4MPASS_ENV_SIGNING_KEY,
      "<path>", "Signing key path for bootchain"     },
    { "ssh-timeout",  TR4MPASS_ENV_RAMDISK_TIMEOUT,
      "<sec>",  "Ramdisk SSH readiness timeout"      },
};

static const size_t g_flag_map_count =
    sizeof(g_flag_map) / sizeof(g_flag_map[0]);

/* -------------------------------------------------------------- */
/* Lookup helpers                                                 */
/* -------------------------------------------------------------- */

static const flag_map_t *find_exact(const char *name, size_t name_len)
{
    for (size_t i = 0; i < g_flag_map_count; ++i) {
        const char *ln = g_flag_map[i].long_name;
        if (strlen(ln) == name_len && strncmp(ln, name, name_len) == 0)
            return &g_flag_map[i];
    }
    return NULL;
}

static int is_secret(const char *env_name)
{
    return env_name != NULL &&
           (strstr(env_name, "PASSWORD") != NULL ||
            strstr(env_name, "SIGNING_KEY") != NULL);
}

static int apply_env(const char *env_name, const char *value)
{
    if (!env_name || !value)
        return -1;
    if (setenv(env_name, value, 1) != 0) {
        log_error("setenv(%s) failed", env_name);
        return -1;
    }
    log_debug("cli_flags: %s <- %s", env_name,
              is_secret(env_name) ? "<secret>" : value);
    return 0;
}

/* -------------------------------------------------------------- */
/* Parsing                                                        */
/* -------------------------------------------------------------- */

int cli_parse_bypass_flags(int argc, char *argv[])
{
    if (argc <= 1 || !argv)
        return 0;

    int rc = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!a || a[0] != '-' || a[1] != '-' || a[2] == '\0')
            continue; /* not a long option */

        const char *name = a + 2;
        const char *eq   = strchr(name, '=');
        size_t      nlen = eq ? (size_t)(eq - name) : strlen(name);

        const flag_map_t *m = find_exact(name, nlen);
        if (!m)
            continue; /* owned by main.c or unknown -- leave alone */

        const char *value = NULL;
        if (eq) {
            value = eq + 1;
        } else if (i + 1 < argc) {
            const char *next = argv[i + 1];
            if (!next) {
                log_error("--%s requires an argument", m->long_name);
                rc = -1;
                continue;
            }
            value = next;
            ++i; /* consume value */
        } else {
            log_error("--%s requires an argument", m->long_name);
            rc = -1;
            continue;
        }

        if (apply_env(m->env_name, value) != 0)
            rc = -1;
    }

    return rc;
}

void cli_flags_print_help(void)
{
    printf("  Bypass pipeline overrides (also read from environment):\n");
    for (size_t i = 0; i < g_flag_map_count; ++i) {
        char head[64];
        snprintf(head, sizeof(head), "--%s %s",
                 g_flag_map[i].long_name,
                 g_flag_map[i].metavar);
        printf("      %-28s %s (%s)\n", head,
               g_flag_map[i].description,
               g_flag_map[i].env_name);
    }
}
