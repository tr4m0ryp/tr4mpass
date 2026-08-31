/*
 * errors.c -- typed error taxonomy for tr4mpass (T10).
 *
 * See include/util/errors.h for the scope note on the ~250 remaining
 * bare `return -1` sites this pass did not migrate.
 */

#include "util/errors.h"
#include "util/log.h"

const char *err_category_str(tr4_err_t category)
{
    switch (category) {
    case ERR_ENV:               return "env";
    case ERR_BUILD_DRIFT:       return "build-drift";
    case ERR_DEVICE_UNSEEN:     return "device-unseen";
    case ERR_DFU_MODE:          return "dfu-mode";
    case ERR_CPID_UNPARSED:     return "cpid-unparsed";
    case ERR_CHIP_UNSUPPORTED:  return "chip-unsupported";
    case ERR_EXPLOIT_FAILED:    return "exploit-failed";
    case ERR_SERVER_ACTIVATION: return "server-activation";
    default:                    return "unknown";
    }
}

int err_ctx(tr4_err_t category, const char *diagnostic, const char *next_step)
{
    log_error("[-] %s: %s. Next: %s. "
              "(See `tr4mpass doctor` for full environment report.)",
              err_category_str(category),
              (diagnostic && *diagnostic) ? diagnostic : "no further detail available",
              (next_step && *next_step) ? next_step : "run `tr4mpass doctor`");
    return -1;
}
