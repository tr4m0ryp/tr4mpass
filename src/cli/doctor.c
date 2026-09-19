/*
 * doctor.c -- `tr4mpass doctor` preflight diagnostic subcommand (T10).
 *
 * Dependency-light: the only external processes this shells out to
 * are `pkg-config`, `command -v lsusb`, `lsusb`, and `pgrep` -- all
 * fixed, argv-safe command strings with no user input threaded into
 * them. Every popen() here is deliberately a compile-time string
 * literal for that reason.
 */

#include "cli/doctor.h"
#include "compat/plist_compat.h"
#include "compat/libirecovery_compat.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { CHK_OK, CHK_WARN, CHK_FAIL } chk_status_t;

static int g_warn_count;
static int g_fail_count;

/* ------------------------------------------------------------------ */
/* Report line + shell-out helper                                     */
/* ------------------------------------------------------------------ */

static void report(chk_status_t status, const char *fmt, ...)
{
    const char *tag;

    switch (status) {
    case CHK_WARN: tag = "[WARN]"; g_warn_count++; break;
    case CHK_FAIL: tag = "[FAIL]"; g_fail_count++; break;
    case CHK_OK:
    default:       tag = "[OK]  "; break;
    }

    printf("  %s ", tag);
    {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    printf("\n");
}

/*
 * Run a fixed shell command via popen(), capture its first line of
 * stdout (trimmed) into `out` (may be NULL/0 to discard output), and
 * return the command's exit status (-1 if popen/pclose itself
 * failed).
 */
static int run_capture(const char *cmd, char *out, size_t outsz)
{
    if (out && outsz)
        out[0] = '\0';

    FILE *fp = popen(cmd, "r"); /* NOLINT: cmd is always a fixed literal */
    if (!fp)
        return -1;

    if (out && outsz && fgets(out, (int)outsz, fp) != NULL) {
        size_t len = strlen(out);
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
            out[--len] = '\0';
    }

    int rc = pclose(fp);
    if (rc == -1)
        return -1;
#ifdef WIFEXITED
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return -1;
#else
    return rc;
#endif
}

/* ------------------------------------------------------------------ */
/* Individual checks                                                  */
/* ------------------------------------------------------------------ */

/* Kept in sync with the authoritative pkg-config list in Makefile
 * (PKG_LIBS) and start.sh (PKGCONFIG_DEPS) -- unified there under T1. */
static const char *const g_pkg_libs[] = {
    "libimobiledevice-1.0",
    "libirecovery-1.0",
    "libusb-1.0",
    "libplist-2.0",
    "openssl",
    "libcurl",
    "libssh2",
};
#define NUM_PKG_LIBS (sizeof(g_pkg_libs) / sizeof(g_pkg_libs[0]))

static void check_pkgconfig(void)
{
    printf("\n-- pkg-config dependencies --\n");
    for (size_t i = 0; i < NUM_PKG_LIBS; i++) {
        char cmd[128];
        char version[64];

        snprintf(cmd, sizeof(cmd), "pkg-config --modversion %s 2>/dev/null",
                  g_pkg_libs[i]);
        int rc = run_capture(cmd, version, sizeof(version));
        if (rc == 0 && version[0])
            report(CHK_OK, "%-24s version %s", g_pkg_libs[i], version);
        else
            report(CHK_FAIL, "%-24s not found by pkg-config", g_pkg_libs[i]);
    }
}

static void check_libplist_compat(void)
{
    printf("\n-- libplist compat shim (include/compat/plist_compat.h) --\n");
#ifdef TP_PLIST_HAS_MEM_FREE
    report(CHK_OK, "plist_mem_free() available (libplist >= 2.3)");
#else
    report(CHK_WARN,
           "plist_mem_free() not available; using free() fallback (libplist < 2.3)");
#endif
#ifdef TP_PLIST_HAS_FMT_ARG
    report(CHK_OK, "plist_from_memory() 4-arg form available (libplist >= 2.3)");
#else
    report(CHK_WARN,
           "plist_from_memory() 4-arg form not available; using 3-arg fallback (libplist < 2.3)");
#endif
}

static void check_libirecovery_compat(void)
{
    printf("\n-- libirecovery compat shim (include/compat/libirecovery_compat.h) --\n");
#if TP_IRECV_NEEDS_MANUAL_ZLP
    report(CHK_WARN,
           "IRECV_SEND_OPT_DFU_NOTIFY_FINISH not available; manual zero-length-packet fallback in use");
#else
    report(CHK_OK, "IRECV_SEND_OPT_DFU_NOTIFY_FINISH available");
#endif
}

/* .build-flags is written by `make` (see Makefile's .build-flags
 * target) with the same three probe results baked into the compat
 * headers above -- shown here for cross-verification against what
 * this particular binary was actually compiled with. */
static void check_build_flags(int verbose)
{
    printf("\n-- build-time probe flags (.build-flags) --\n");

    FILE *fp = fopen(".build-flags", "r");
    if (!fp) {
        report(CHK_WARN,
               ".build-flags not found in the current directory -- run from the repo root after `make` to cross-check probe results");
        return;
    }

    char line[128];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (line[0] == '\0')
            continue;
        count++;
        if (verbose)
            report(CHK_OK, "%s", line);
    }
    fclose(fp);

    if (!verbose)
        report(CHK_OK, "%d probe result(s) recorded (pass -v for details)", count);
}

static void check_usbmuxd(void)
{
    printf("\n-- usbmuxd --\n");

    /* pgrep -x is the same test used by start-helpers.sh's hints
     * (systemctl status usbmuxd) -- portable to macOS and Linux. */
    int rc = run_capture("pgrep -x usbmuxd 2>/dev/null", NULL, 0);
    if (rc == 0) {
        report(CHK_OK, "usbmuxd process is running");
        return;
    }

    struct stat st;
    if (stat("/var/run/usbmuxd", &st) == 0) {
        report(CHK_OK, "usbmuxd socket present at /var/run/usbmuxd");
        return;
    }

    report(CHK_WARN,
           "usbmuxd not detected (no process, no /var/run/usbmuxd socket) -- normal-mode device queries will fail; "
           "start it (Linux: sudo systemctl start usbmuxd)");
}

static void check_usb_visibility(void)
{
    printf("\n-- USB visibility (Apple vendor 05ac) --\n");

    char path[256];
    int rc = run_capture("command -v lsusb 2>/dev/null", path, sizeof(path));
    if (rc != 0 || path[0] == '\0') {
        report(CHK_WARN,
               "lsusb not available on this platform (expected on macOS) -- skipping USB visibility check");
        return;
    }

    char out[64];
    run_capture("lsusb 2>/dev/null | grep -ci '05ac:'", out, sizeof(out));
    int count = out[0] ? atoi(out) : 0;

    if (count > 0)
        report(CHK_OK, "%d Apple (05ac) USB device(s) visible via lsusb", count);
    else
        report(CHK_WARN,
               "No Apple (05ac) USB device visible via lsusb -- connect the device (and its cable/port) or enter DFU mode");
}

static void check_wsl(void)
{
    printf("\n-- WSL / environment --\n");

    const char *distro = getenv("WSL_DISTRO_NAME");
    int is_wsl = (distro && *distro) ? 1 : 0;

    FILE *fp = fopen("/proc/version", "r");
    if (fp) {
        char buf[512];
        if (fgets(buf, sizeof(buf), fp)) {
            for (char *p = buf; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            if (strstr(buf, "microsoft") || strstr(buf, "wsl"))
                is_wsl = 1;
        }
        fclose(fp);
    }

    if (!is_wsl) {
        report(CHK_OK, "Not running under WSL");
        return;
    }

    report(CHK_WARN,
           "Running under WSL%s%s -- USB devices need usbipd-win passthrough (`usbipd attach --wsl --busid <id>`)",
           (distro && *distro) ? ": " : "", (distro && *distro) ? distro : "");

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) &&
        (strncmp(cwd, "/mnt/c", 6) == 0 || strncmp(cwd, "/mnt/d", 6) == 0)) {
        report(CHK_FAIL,
               "Checked out on a Windows-mounted path (%s) -- clone into your Linux home instead (e.g. ~/tr4mpass) for working USB passthrough and I/O performance",
               cwd);
    }
}

static void check_binary_shadow(void)
{
    printf("\n-- binary shadowing --\n");

    struct stat st;
    if (stat("./tr4mpass", &st) != 0) {
        report(CHK_WARN,
               "./tr4mpass not found in the current directory -- run doctor from the repo root after `make`, or ignore this if invoked as `tr4mpass doctor` from PATH");
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        report(CHK_FAIL,
               "./tr4mpass exists but is a DIRECTORY, not the binary -- remove it and rebuild: rm -rf ./tr4mpass && make clean && make");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        report(CHK_WARN, "./tr4mpass exists but is not a regular file (mode 0%o)",
               (unsigned)(st.st_mode & 07777u));
        return;
    }

    report(CHK_OK, "./tr4mpass is a regular file");
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

int doctor_run(int verbose)
{
    g_warn_count = 0;
    g_fail_count = 0;

    printf("========================================\n"
           "  tr4mpass doctor -- environment preflight\n"
           "========================================\n");

    check_pkgconfig();
    check_libplist_compat();
    check_libirecovery_compat();
    check_build_flags(verbose);
    check_usbmuxd();
    check_usb_visibility();
    check_wsl();
    check_binary_shadow();

    printf("\n========================================\n");
    if (g_fail_count > 0)
        printf("  Summary: %d FAIL, %d WARN -- fix the FAIL item(s) above before running tr4mpass.\n",
               g_fail_count, g_warn_count);
    else if (g_warn_count > 0)
        printf("  Summary: 0 FAIL, %d WARN -- tr4mpass should run, but review the warning(s) above.\n",
               g_warn_count);
    else
        printf("  Summary: all checks passed.\n");
    printf("========================================\n");

    return (g_fail_count > 0) ? 1 : 0;
}
