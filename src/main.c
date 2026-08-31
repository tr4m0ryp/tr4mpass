/* main.c -- tr4mpass entry point and orchestration. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include "tr4mpass.h"
#include "device/device.h"
#include "device/usb_dfu.h"
#include "device/chip_db.h"
#include "bypass/bypass.h"
#include "bypass/path_a.h"
#include "bypass/path_b.h"
#include "activation/session.h"
#include "activation/activation.h"
#include "util/log.h"
#include "util/env_config.h"
#include "util/errors.h"
#include "cli/cli_flags.h"
#include "cli/doctor.h"

typedef struct {
    int verbose;
    int dry_run;
    int detect_only;
    int activate_only;
    int probe_albert;
    int force_path_a;
    int force_path_b;
    uint32_t cpid_override;
    uint64_t ecid_override;
    int has_cpid_override;
    int has_ecid_override;
} cli_opts_t;

static void print_banner(void)
{
    printf("========================================\n"
           "  tr4mpass v%s\n"
           "  Activation lock bypass research tool\n"
           "========================================\n\n", TR4MPASS_VERSION);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n"
           "       %s doctor [-v]      Run environment preflight diagnostics and exit\n\n"
           "Options:\n"
           "  -v, --verbose          Enable debug logging + env summary\n"
           "  -n, --dry-run          Show what would run, do not execute\n"
           "  -d, --detect-only      Detect device and exit\n"
           "      --activate-only    Online activation via Albert (normal mode)\n"
           "      --probe-albert     Dump IngestBody fields + drmHandshake only\n"
           "  -a, --force-path-a     Force Path A (checkm8, A5-A11)\n"
           "  -b, --force-path-b     Force Path B (identity, A12+)\n"
           "      --cpid <hex>       Override CPID (e.g. --cpid 0x8960)\n"
           "      --ecid <hex>       Override ECID (e.g. --ecid 0x1F70CB1331C)\n"
           "  -h, --help             Show this help message\n\n",
           prog, prog);
    cli_flags_print_help();
}

static int parse_args(int argc, char *argv[], cli_opts_t *opts)
{
    enum {
        OPT_CPID          = 256,
        OPT_ECID          = 257,
        OPT_ACTIVATE_ONLY = 258,
        OPT_PROBE_ALBERT  = 259,
    };

    static const struct option longopts[] = {
        { "verbose",        no_argument,       NULL, 'v' },
        { "dry-run",        no_argument,       NULL, 'n' },
        { "detect-only",    no_argument,       NULL, 'd' },
        { "activate-only",  no_argument,       NULL, OPT_ACTIVATE_ONLY },
        { "probe-albert",   no_argument,       NULL, OPT_PROBE_ALBERT  },
        { "force-path-a",   no_argument,       NULL, 'a' },
        { "force-path-b",   no_argument,       NULL, 'b' },
        { "cpid",           required_argument, NULL, OPT_CPID },
        { "ecid",           required_argument, NULL, OPT_ECID },
        { "help",           no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    memset(opts, 0, sizeof(*opts));

    /*
     * Silence getopt's own errno print for unknown flags -- the
     * bypass-pipeline long options (handled later by
     * cli_parse_bypass_flags) show up here as '?' and should not
     * abort the program.
     */
    opterr = 0;

    int c;
    while ((c = getopt_long(argc, argv, "vndabh", longopts, NULL)) != -1) {
        switch (c) {
        case 'v': opts->verbose        = 1; break;
        case 'n': opts->dry_run        = 1; break;
        case 'd': opts->detect_only    = 1; break;
        case OPT_ACTIVATE_ONLY:
                  opts->activate_only  = 1; break;
        case OPT_PROBE_ALBERT:
                  opts->probe_albert   = 1; break;
        case 'a': opts->force_path_a   = 1; break;
        case 'b': opts->force_path_b   = 1; break;
        case OPT_CPID:
            opts->cpid_override     = (uint32_t)strtoul(optarg, NULL, 16);
            opts->has_cpid_override = 1;
            break;
        case OPT_ECID:
            opts->ecid_override     = (uint64_t)strtoull(optarg, NULL, 16);
            opts->has_ecid_override = 1;
            break;
        case 'h': print_usage(argv[0]); return 1;
        case '?': /* unknown flag -- defer to cli_parse_bypass_flags */
            break;
        default:  print_usage(argv[0]); return -1;
        }
    }

    if (opts->force_path_a && opts->force_path_b) {
        log_error("Cannot force both Path A and Path B simultaneously");
        return -1;
    }

    return 0;
}

static int detect_device(device_info_t *dev)
{
    libusb_device_handle *usb_handle = NULL;
    uint8_t iserial = 0;
    memset(dev, 0, sizeof(*dev));

    if (usb_dfu_find(&usb_handle, &iserial) == 0) {
        log_info("Device found in DFU mode (iSerial index: %u)",
                 (unsigned)iserial);
        uint32_t cpid = 0;
        uint64_t ecid = 0;
        char serial[DFU_SERIAL_MAX] = {0};
        if (usb_dfu_read_info(usb_handle, iserial, &cpid, &ecid,
                              serial, sizeof(serial)) < 0)
            log_warn("Failed to read DFU serial info");
        dev->cpid          = cpid;
        dev->ecid          = ecid;
        dev->is_dfu_mode   = 1;
        dev->usb           = usb_handle;
        dev->iserial_index = iserial;
        snprintf(dev->serial, sizeof(dev->serial), "%s", serial);
        return 0;
    }

    log_info("No DFU device found, trying normal mode...");
    if (device_detect(dev) < 0) {
        return err_ctx(ERR_DEVICE_UNSEEN,
                        "No device detected on any known USB interface",
                        "Check the USB cable/port and that the device is powered on, then run `tr4mpass doctor` to verify usbmuxd and USB visibility");
    }
    if (device_query_info(dev) < 0)
        log_warn("Incomplete device info (partial data may be available)");
    return 0;
}

static void enrich_chip_info(device_info_t *dev)
{
    if (dev->cpid == 0) {
        log_warn("CPID not available, chip lookup skipped");
        return;
    }
    const chip_info_t *chip = chip_db_lookup(dev->cpid);
    if (!chip) {
        log_warn("CPID 0x%04X not found in chip database", dev->cpid);
        snprintf(dev->chip_name, sizeof(dev->chip_name), "Unknown");
        dev->checkm8_vulnerable = 0;
        return;
    }
    snprintf(dev->chip_name, sizeof(dev->chip_name), "%s", chip->name);
    dev->checkm8_vulnerable = chip->checkm8_vulnerable;
    log_debug("Chip: %s (CPID 0x%04X) -- checkm8 %s",
              chip->name, chip->cpid,
              chip->checkm8_vulnerable ? "vulnerable" : "not vulnerable");
}

static void register_modules(void)
{
    if (bypass_register(&path_a_module) < 0)
        log_warn("Failed to register Path A module");
    if (bypass_register(&path_b_module) < 0)
        log_warn("Failed to register Path B module");
    log_debug("Registered %d bypass module(s)", bypass_count());
}

static void print_module_diagnostics(const device_info_t *dev)
{
    char diag[256];

    snprintf(diag, sizeof(diag),
             "No compatible bypass module (DFU=%s CPID=0x%04X Chip=%s checkm8=%s Product=%s)",
             dev->is_dfu_mode ? "YES" : "NO", dev->cpid,
             dev->chip_name[0] ? dev->chip_name : "(unknown)",
             dev->checkm8_vulnerable ? "YES" : "NO",
             dev->product_type[0] ? dev->product_type : "(unknown)");

    if (!dev->is_dfu_mode) {
        err_ctx(ERR_DFU_MODE, diag,
                "Both paths require DFU mode -- use ./start.sh or enter DFU manually");
    } else if (dev->cpid == 0) {
        err_ctx(ERR_CPID_UNPARSED, diag,
                "CPID is 0x0000 -- try --cpid to set it manually");
    } else if (dev->chip_name[0] == '\0' || strcmp(dev->chip_name, "Unknown") == 0) {
        err_ctx(ERR_CHIP_UNSUPPORTED, diag,
                "Chip not found in the chip database");
    } else {
        err_ctx(ERR_CHIP_UNSUPPORTED, diag,
                "No registered bypass module matched this device");
    }
}

static const bypass_module_t *select_module(device_info_t *dev,
                                            const cli_opts_t *opts)
{
    if (opts->force_path_a) { log_info("Path A forced"); return &path_a_module; }
    if (opts->force_path_b) { log_info("Path B forced"); return &path_b_module; }
    const bypass_module_t *mod = bypass_select(dev);
    if (!mod) { print_module_diagnostics(dev); return NULL; }
    log_info("Auto-selected module: %s", mod->name);
    return mod;
}

static void cleanup(device_info_t *dev)
{
    if (dev->usb) {
        usb_dfu_close(dev->usb);
        dev->usb = NULL;
    }
    device_free(dev);
    usb_dfu_cleanup();
}

int main(int argc, char *argv[])
{
    cli_opts_t opts;
    device_info_t dev;
    int ret;

    /*
     * `tr4mpass doctor` is a standalone preflight report -- it needs
     * no USB init, no device detect, and no getopt parsing of the
     * normal bypass-pipeline flags. Dispatch to it before anything
     * else in main() runs.
     */
    if (argc >= 2 && strcmp(argv[1], "doctor") == 0) {
        int doctor_verbose = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
                doctor_verbose = 1;
        }
        return doctor_run(doctor_verbose);
    }

    print_banner();

    /*
     * Apply bypass-pipeline CLI overrides first so argv is still
     * pristine (main.c's getopt_long below will permute argv).
     */
    if (cli_parse_bypass_flags(argc, argv) != 0) {
        log_error("Failed to parse bypass-pipeline CLI flags");
        return 1;
    }

    ret = parse_args(argc, argv, &opts);
    if (ret != 0)
        return (ret > 0) ? 0 : 1;
    if (opts.verbose) {
        log_set_level(LOG_DEBUG);
        env_print_tr4mpass_summary();
    }

    if (usb_dfu_init() < 0) {
        log_error("Failed to initialize USB subsystem");
        return 1;
    }
    if (detect_device(&dev) < 0) {
        usb_dfu_cleanup();
        return 1;
    }

    /*
     * Apply CLI overrides for CPID/ECID.  This runs after
     * usb_dfu_read_info() has probed every descriptor index it knows;
     * gating on `dev.<field> == 0` means overrides only fire when the
     * device did not surface the value itself (either the descriptor
     * layout is undocumented or the device is not in true SecureROM
     * DFU).  If either override applied, emit a single "manual
     * override" line so bug reports show the fallback ran.
     */
    int cpid_from_override = 0;
    int ecid_from_override = 0;
    if (opts.has_cpid_override) {
        if (dev.cpid == 0) {
            dev.cpid = opts.cpid_override;
            log_info("CPID overridden: 0x%04X (from --cpid)", dev.cpid);
            cpid_from_override = 1;
        } else {
            log_info("CPID already detected (0x%04X), ignoring --cpid override",
                     dev.cpid);
        }
    }
    if (opts.has_ecid_override) {
        if (dev.ecid == 0) {
            dev.ecid = opts.ecid_override;
            log_info("ECID overridden: 0x%llX (from --ecid)",
                     (unsigned long long)dev.ecid);
            ecid_from_override = 1;
        } else {
            log_info("ECID already detected (0x%llX), ignoring --ecid override",
                     (unsigned long long)dev.ecid);
        }
    }
    if (cpid_from_override || ecid_from_override)
        log_info("[device] using --cpid=0x%04X --ecid=0x%llX manual override",
                 dev.cpid, (unsigned long long)dev.ecid);

    enrich_chip_info(&dev);
    device_print_info(&dev);

    if (opts.detect_only) {
        log_info("Detect-only mode, exiting");
        cleanup(&dev);
        return 0;
    }

    if (opts.probe_albert) {
        plist_t session_info = NULL;
        int prc = -1;

        if (!dev.handle) {
            log_error("--probe-albert requires device in normal mode with lockdownd");
            cleanup(&dev);
            return 1;
        }

        log_info("=== Albert probe: IngestBody dump + drmHandshake only ===");

        if (session_get_info(&dev, &session_info) == 0)
            prc = session_probe_albert(&dev, session_info);

        if (session_info) plist_free(session_info);
        cleanup(&dev);
        return (prc == 0) ? 0 : 1;
    }

    if (opts.activate_only) {
        plist_t session_info  = NULL;
        plist_t hs_response   = NULL;
        plist_t activ_info    = NULL;
        plist_t activ_record  = NULL;
        int arc = -1;

        if (!dev.handle) {
            log_error("--activate-only requires device in normal mode with lockdownd");
            cleanup(&dev);
            return 1;
        }

        log_info("=== Online activation via Albert server ===");

        if (session_get_info(&dev, &session_info) != 0)                          goto ao_done;
        if (session_drm_handshake_online(&dev, session_info, &hs_response) != 0) goto ao_done;
        if (session_create_activation_info(&dev, hs_response, &activ_info) != 0) goto ao_done;
        if (session_device_activation_online(&dev, activ_info, &activ_record) != 0) goto ao_done;
        if (session_activate(&dev, activ_record, hs_response) != 0)              goto ao_done;

        arc = activation_is_activated(&dev);
        if (arc == 1) {
            printf("\n[+] Device activated successfully.\n");
            arc = 0;
        } else {
            printf("\n[-] Activation step completed but device not yet activated.\n");
            arc = 1;
        }

ao_done:
        if (session_info) plist_free(session_info);
        if (hs_response)  plist_free(hs_response);
        if (activ_info)   plist_free(activ_info);
        if (activ_record) plist_free(activ_record);
        cleanup(&dev);
        return arc < 0 ? 1 : arc;
    }

    register_modules();
    bypass_list();

    const bypass_module_t *mod = select_module(&dev, &opts);
    if (!mod) { cleanup(&dev); return 1; }

    if (opts.dry_run) {
        printf("[dry-run] Would execute: %s\n", mod->name);
        printf("[dry-run] Description:   %s\n", mod->description);
        cleanup(&dev);
        return 0;
    }

    log_info("Executing module: %s", mod->name);
    ret = bypass_execute(mod, &dev);
    if (ret == 0) {
        printf("\n[+] Bypass completed successfully.\n");
    } else {
        char diag[128];
        snprintf(diag, sizeof(diag), "Bypass module '%s' failed (error %d)",
                 mod->name, ret);
        err_ctx(ERR_EXPLOIT_FAILED, diag,
                "Re-check DFU mode and cable/port, or retry with --force-path-a/--force-path-b or --cpid/--ecid overrides");
    }

    cleanup(&dev);
    return (ret == 0) ? 0 : 1;
}
