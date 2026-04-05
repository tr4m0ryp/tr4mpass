/*
 * deletescript.h -- Post-bypass iOS cleanup operations.
 *
 * Performs iOS-version-specific cleanup after activation bypass:
 * remove Setup.app, set PurpleBuddy done, purge activation records,
 * and clear system caches. Uses AFC for all file operations.
 */

#ifndef DELETESCRIPT_H
#define DELETESCRIPT_H

#include "device/device.h"

typedef struct {
    unsigned int ios_major;
    unsigned int ios_minor;
    unsigned int ios_patch;
} ios_version_t;

/*
 * deletescript_parse_version -- Parse iOS version string into components.
 * Accepts formats: "16.3.1", "17.0", "26.1". Missing components are 0.
 * Returns 0 on success, -1 on error.
 */
int deletescript_parse_version(const char *version_str, ios_version_t *ver);

/*
 * deletescript_run -- Run the full deletescript for the device.
 * Auto-detects iOS version and runs all appropriate cleanup steps.
 * Requires an active lockdown connection on the device.
 * Returns 0 on success, -1 on error.
 */
int deletescript_run(device_info_t *dev);

/*
 * deletescript_remove_setup_app -- Rename Setup.app to Setup.app.bak.
 * Disables the activation lock setup screen on next boot.
 * Returns 0 on success, -1 on error.
 */
int deletescript_remove_setup_app(device_info_t *dev);

/*
 * deletescript_set_purplebuddy_done -- Set SetupDone=true in purplebuddy plist.
 * Writes to /private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist.
 * Returns 0 on success, -1 on error.
 */
int deletescript_set_purplebuddy_done(device_info_t *dev);

/*
 * deletescript_purge_activation_records -- Remove all activation records.
 * Purges both /var/root/Library/Lockdown/activation_records/ and
 * /var/mobile/Library/mad/activation_records/ directories.
 * Returns 0 on success, -1 on error.
 */
int deletescript_purge_activation_records(device_info_t *dev);

/*
 * deletescript_clear_caches -- Clear system caches.
 * Removes cached activation and setup state that could interfere
 * with the bypass persisting across reboots.
 * Returns 0 on success, -1 on error.
 */
int deletescript_clear_caches(device_info_t *dev);

#endif /* DELETESCRIPT_H */
