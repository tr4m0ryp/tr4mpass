/*
 * env_config.h -- Thin wrapper around getenv that logs a warning when
 * required tr4mpass environment variables are missing.
 *
 * Centralizes lookup, defaulting, and summary printing for every
 * TR4MPASS_* variable used by the bypass pipeline.  Phase 2B and 2C
 * read configuration through these helpers; Phase 2D (this module)
 * supplies CLI-flag shims in src/cli/cli_flags.c that set the env
 * vars before the bypass code reads them.
 */

#ifndef TR4MPASS_ENV_CONFIG_H
#define TR4MPASS_ENV_CONFIG_H

#include <stddef.h>

/*
 * Recognized TR4MPASS_* environment variables.  Update the table in
 * env_config.c when adding new names so --verbose keeps printing the
 * complete picture.
 */
#define TR4MPASS_ENV_RAMDISK_PATH      "TR4MPASS_RAMDISK_PATH"
#define TR4MPASS_ENV_IBSS_PATH         "TR4MPASS_IBSS_PATH"
#define TR4MPASS_ENV_IPSW_PATH         "TR4MPASS_IPSW_PATH"
#define TR4MPASS_ENV_IBEC_PATH         "TR4MPASS_IBEC_PATH"
#define TR4MPASS_ENV_DEVICETREE_PATH   "TR4MPASS_DEVICETREE_PATH"
#define TR4MPASS_ENV_TRUSTCACHE_PATH   "TR4MPASS_TRUSTCACHE_PATH"
#define TR4MPASS_ENV_PATCHED_MAD       "TR4MPASS_PATCHED_MAD"
#define TR4MPASS_ENV_SSH_HOST          "TR4MPASS_SSH_HOST"
#define TR4MPASS_ENV_SSH_PORT          "TR4MPASS_SSH_PORT"
#define TR4MPASS_ENV_SSH_USER          "TR4MPASS_SSH_USER"
#define TR4MPASS_ENV_SSH_PASSWORD      "TR4MPASS_SSH_PASSWORD"
#define TR4MPASS_ENV_SIGNING_KEY       "TR4MPASS_SIGNING_KEY"
#define TR4MPASS_ENV_RAMDISK_TIMEOUT   "TR4MPASS_RAMDISK_TIMEOUT"

/*
 * Return the value of `name` from the environment.  When the variable
 * is not set, `default_value` is returned (which may itself be NULL).
 * An empty string ("") is returned as-is and is *not* replaced with
 * the default -- callers that treat empty as unset must check.
 */
const char *env_get(const char *name, const char *default_value);

/*
 * Parse `name` as a base-10 integer.  Returns `default_value` if the
 * variable is unset, empty, or cannot be fully consumed as a number.
 */
int env_get_int(const char *name, int default_value);

/*
 * Verify every name in `names[0..count)` is set and non-empty.
 * Returns 0 when all are present.  Returns -1 and writes a warning
 * listing every missing name when at least one is absent.
 */
int env_require_all(const char *const *names, size_t count);

/*
 * Dump the current value of every TR4MPASS_* variable to stdout.
 * Secrets (password, signing key) are abbreviated; paths are printed
 * verbatim; unset variables print "<unset>".
 */
void env_print_tr4mpass_summary(void);

#endif /* TR4MPASS_ENV_CONFIG_H */
