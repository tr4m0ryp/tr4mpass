#ifndef TR4MPASS_ERRORS_H
#define TR4MPASS_ERRORS_H

/*
 * errors.h -- typed error taxonomy for tr4mpass (T10).
 *
 * The tool's #1 UX failure historically was the silent bailout: a
 * bare `return -1` with no category and no next-step, surfaced (if at
 * all) as an unhelpful "[-] Bypass failed (error -1)". err_ctx()
 * gives every failure path a category, a one-line diagnostic, and a
 * one-line next step, and always points the user at `tr4mpass doctor`
 * for the full environment report.
 *
 * Scope note: the codebase has on the order of ~250 bare `return -1`
 * / log_error() sites; migrating all of them to err_ctx() is tracked
 * as follow-up work and out of scope for this pass -- src/main.c's
 * own error returns (detect_device(), print_module_diagnostics(), and
 * the final bypass-failed line) have been migrated here as the
 * flagship example of the new pattern.
 */

typedef enum {
    ERR_ENV = 0,          /* missing/broken environment (deps, usbmuxd, WSL, ...) */
    ERR_BUILD_DRIFT,      /* library API drift the compat shims did not cover */
    ERR_DEVICE_UNSEEN,    /* no device visible on any known USB interface */
    ERR_DFU_MODE,         /* device not in the required DFU mode */
    ERR_CPID_UNPARSED,    /* CPID could not be parsed from the DFU descriptor */
    ERR_CHIP_UNSUPPORTED, /* CPID parsed but not in the chip database / no module */
    ERR_EXPLOIT_FAILED,   /* a bypass module ran and failed */
    ERR_SERVER_ACTIVATION /* the Albert/session-activation flow failed */
} tr4_err_t;

/*
 * Log a categorized, actionable error via the existing log_error()
 * and return -1, so call sites can simply `return err_ctx(...)`.
 *
 * Prints (through log_error(), which adds its own "[ERROR]" tag and
 * color):
 *
 *   [-] <category>: <diagnostic>. Next: <next_step>.
 *   (See `tr4mpass doctor` for full environment report.)
 *
 * `diagnostic` and `next_step` may be NULL; a generic placeholder is
 * substituted so the format string never prints "(null)".
 */
int err_ctx(tr4_err_t category, const char *diagnostic, const char *next_step);

/* Short, stable category string for a tr4_err_t, e.g.
 * ERR_DFU_MODE -> "dfu-mode". Used by err_ctx() and safe to call
 * directly wherever a category label is needed without logging. */
const char *err_category_str(tr4_err_t category);

#endif /* TR4MPASS_ERRORS_H */
