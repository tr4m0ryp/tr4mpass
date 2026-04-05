/*
 * deletescript.c -- Post-bypass iOS cleanup via AFC.
 *
 * Renames Setup.app, sets PurpleBuddy done, purges activation records,
 * and clears system caches. All file ops go through afc_utils.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bypass/deletescript.h"
#include "bypass/afc_utils.h"
#include "util/log.h"

#define LOG_TAG "[deletescript]"

/* Root filesystem access requires the jailbroken AFC2 service */
#define AFC2_SERVICE_NAME      "com.apple.afc2"

#define SETUP_APP_PATH         "/Applications/Setup.app"
#define SETUP_APP_BAK          "/Applications/Setup.app.bak"
#define PURPLEBUDDY_PLIST      "/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist"
#define ACT_RECORDS_ROOT       "/var/root/Library/Lockdown/activation_records"
#define ACT_RECORDS_MOBILE     "/var/mobile/Library/mad/activation_records"
#define CACHE_DIR_PURPLEBUDDY  "/var/mobile/Library/Caches/com.apple.purplebuddy"

int deletescript_parse_version(const char *version_str, ios_version_t *ver)
{
    int n;
    if (!version_str || !ver) {
        log_error("%s NULL argument to parse_version", LOG_TAG);
        return -1;
    }
    memset(ver, 0, sizeof(*ver));
    n = sscanf(version_str, "%u.%u.%u",
               &ver->ios_major, &ver->ios_minor, &ver->ios_patch);
    if (n < 1) {
        log_error("%s Cannot parse version: '%s'", LOG_TAG, version_str);
        return -1;
    }
    /* Sanity check: major must be positive and reasonable */
    if (ver->ios_major == 0 || ver->ios_major >= 100) {
        log_error("%s Unreasonable iOS version: %u.%u.%u",
                  LOG_TAG, ver->ios_major, ver->ios_minor, ver->ios_patch);
        return -1;
    }
    return 0;
}

/* Remove all files in a directory, preserving the directory itself. */
static int purge_directory(afc_client_t afc, const char *dir_path)
{
    char **entries = NULL;
    afc_error_t err;
    int removed = 0;

    err = afc_read_directory(afc, dir_path, &entries);
    if (err != AFC_E_SUCCESS) {
        /* Directory may not exist -- not necessarily an error */
        log_warn("%s Directory '%s' not accessible (may not exist)",
                 LOG_TAG, dir_path);
        return 0;
    }

    for (int i = 0; entries[i] != NULL; i++) {
        char full_path[1024];
        if (strcmp(entries[i], ".") == 0 || strcmp(entries[i], "..") == 0)
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i]);
        if (afc_remove_file(afc, full_path) == 0)
            removed++;
        else
            log_warn("%s Could not remove: %s", LOG_TAG, full_path);
    }

    afc_dictionary_free(entries);
    return removed;
}

int deletescript_remove_setup_app(device_info_t *dev)
{
    afc_client_t afc = NULL;
    afc_error_t err;
    int ret = -1;

    if (!dev) { log_error("%s NULL device", LOG_TAG); return -1; }

    if (afc_connect_service(dev, &afc, AFC2_SERVICE_NAME) < 0)
        return -1;

    if (afc_file_exists(afc, SETUP_APP_PATH) != 1) {
        log_info("%s Setup.app not found (already removed?)", LOG_TAG);
        afc_disconnect(afc);
        return 0;
    }

    /* Rename rather than delete -- safer and reversible */
    err = afc_rename_path(afc, SETUP_APP_PATH, SETUP_APP_BAK);
    if (err != AFC_E_SUCCESS)
        log_error("%s Failed to rename Setup.app: %d", LOG_TAG, (int)err);
    else {
        log_info("%s Renamed Setup.app -> Setup.app.bak", LOG_TAG);
        ret = 0;
    }

    afc_disconnect(afc);
    return ret;
}

int deletescript_set_purplebuddy_done(device_info_t *dev)
{
    afc_client_t afc = NULL;
    int ret = -1;
    /* Minimal XML plist marking setup as complete */
    static const char plist_data[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>SetupDone</key><true/>\n"
        "  <key>SetupFinishedAllSteps</key><true/>\n"
        "  <key>SetupVersion</key><integer>1</integer>\n"
        "</dict>\n"
        "</plist>\n";

    if (!dev) { log_error("%s NULL device", LOG_TAG); return -1; }

    if (afc_connect_service(dev, &afc, AFC2_SERVICE_NAME) < 0)
        return -1;

    if (afc_write_file(afc, PURPLEBUDDY_PLIST,
                       plist_data, (uint32_t)(sizeof(plist_data) - 1)) < 0)
        log_error("%s Failed to write purplebuddy plist", LOG_TAG);
    else {
        log_info("%s Set SetupDone=true", LOG_TAG);
        ret = 0;
    }

    afc_disconnect(afc);
    return ret;
}

int deletescript_purge_activation_records(device_info_t *dev)
{
    afc_client_t afc = NULL;
    int total = 0, n;

    if (!dev) { log_error("%s NULL device", LOG_TAG); return -1; }
    if (afc_connect_service(dev, &afc, AFC2_SERVICE_NAME) < 0)
        return -1;

    n = purge_directory(afc, ACT_RECORDS_ROOT);
    if (n > 0) total += n;
    n = purge_directory(afc, ACT_RECORDS_MOBILE);
    if (n > 0) total += n;

    afc_disconnect(afc);
    log_info("%s Purged %d activation record(s)", LOG_TAG, total);
    return 0;
}

int deletescript_clear_caches(device_info_t *dev)
{
    afc_client_t afc = NULL;
    int n;

    if (!dev) { log_error("%s NULL device", LOG_TAG); return -1; }
    if (afc_connect_service(dev, &afc, AFC2_SERVICE_NAME) < 0)
        return -1;

    n = purge_directory(afc, CACHE_DIR_PURPLEBUDDY);
    log_info("%s Cleared %d cache entries", LOG_TAG, n > 0 ? n : 0);

    /* TODO: enumerate /var/containers/Data/System/ for setupassistant caches.
     * AFC does not support glob patterns. */

    afc_disconnect(afc);
    return 0;
}

int deletescript_run(device_info_t *dev)
{
    ios_version_t ver;
    int errors = 0;

    if (!dev) { log_error("%s NULL device", LOG_TAG); return -1; }

    if (deletescript_parse_version(dev->ios_version, &ver) < 0) {
        log_warn("%s Cannot parse iOS version '%s', using generic cleanup",
                 LOG_TAG, dev->ios_version);
        ver.ios_major = 0;
    }

    log_info("%s Running deletescript for iOS %u.%u.%u on %s",
             LOG_TAG, ver.ios_major, ver.ios_minor, ver.ios_patch,
             dev->product_type);

    /* Steps are safe for all iOS versions; order matters. */
    if (deletescript_remove_setup_app(dev) < 0)          errors++;
    if (deletescript_set_purplebuddy_done(dev) < 0)      errors++;
    if (deletescript_purge_activation_records(dev) < 0)  errors++;
    if (deletescript_clear_caches(dev) < 0)              errors++;

    if (errors > 0) {
        log_warn("%s Completed with %d error(s)", LOG_TAG, errors);
        return -1;
    }

    log_info("%s Deletescript completed successfully", LOG_TAG);
    return 0;
}
