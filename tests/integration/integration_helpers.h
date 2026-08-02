/*
 * integration_helpers.h -- Shared fixtures for Path A / Path B E2E tests.
 *
 * Each E2E test needs a synthetic device_info_t, a set of readable temp
 * files for the iBSS/iBEC/devicetree/ramdisk/patched-mad stages, and a
 * consistent set of TR4MPASS_* environment variables so path_a_*.c and
 * path_b_*.c accept the inputs without reaching real hardware.
 *
 * All paths live under /tmp/tr4mpass_e2e_<pid>/ so concurrent test runs
 * do not collide.  Tests call e2e_fixture_init() in setUp and
 * e2e_fixture_teardown() in tearDown; mock_reset_all() must still be
 * invoked explicitly (the helpers never touch mock state).
 */

#ifndef TR4MPASS_E2E_INTEGRATION_HELPERS_H
#define TR4MPASS_E2E_INTEGRATION_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#include "device/device.h"

#define E2E_PATH_MAX      160  /* base_dir: /tmp/tr4mpass_e2e_<pid>        */
#define E2E_PATH_FULL_MAX 200  /* base_dir + longest suffix (/mobileactivationd) */

typedef struct {
    char base_dir[E2E_PATH_MAX];
    char ibss[E2E_PATH_FULL_MAX];
    char ibec[E2E_PATH_FULL_MAX];
    char devtree[E2E_PATH_FULL_MAX];
    char trustcache[E2E_PATH_FULL_MAX];
    char ramdisk[E2E_PATH_FULL_MAX];
    char patched_mad[E2E_PATH_FULL_MAX];
} e2e_fixture_t;

/*
 * e2e_fixture_init -- Create a fresh /tmp/tr4mpass_e2e_<pid>/ directory
 * populated with minimal placeholder files for each Path A stage.  The
 * TR4MPASS_* environment variables are set to point at those files.
 *
 * Returns 0 on success, -1 on any I/O failure.  On success the caller
 * must call e2e_fixture_teardown() to remove the temp files and unset
 * the environment variables.
 */
int  e2e_fixture_init(e2e_fixture_t *fx);

/*
 * e2e_fixture_teardown -- Delete temp files and unset env vars.  Safe to
 * call even when e2e_fixture_init() failed partway through.
 */
void e2e_fixture_teardown(e2e_fixture_t *fx);

/*
 * e2e_make_device_path_a -- Populate a device_info_t for a Path A
 * (checkm8-vulnerable) device with all identity fields filled in.  The
 * device is returned by value so the caller owns the storage.
 */
device_info_t e2e_make_device_path_a(void);

/*
 * e2e_make_device_path_b_cellular -- A12+ cellular device fixture
 * (checkm8_vulnerable=0, IMEI populated).
 */
device_info_t e2e_make_device_path_b_cellular(void);

/*
 * e2e_make_device_path_b_wifi -- A12+ WiFi-only device fixture
 * (checkm8_vulnerable=0, IMEI and MEID empty).
 */
device_info_t e2e_make_device_path_b_wifi(void);

/*
 * e2e_unset_all_env -- Clear every TR4MPASS_* env var this fixture
 * touches.  Used by the "missing env" test case that wants a completely
 * unconfigured environment.
 */
void e2e_unset_all_env(void);

/*
 * e2e_first_index_of -- Return the index of the first call-log entry
 * containing needle, or -1 if none.  Used by ordering assertions.
 */
int  e2e_first_index_of(const char *needle);

/*
 * e2e_ordering_ok -- Shorthand for "a appears before b in the call log".
 * Returns 1 if both are present and a comes first, 0 otherwise.
 */
int  e2e_ordering_ok(const char *needle_a, const char *needle_b);

#endif /* TR4MPASS_E2E_INTEGRATION_HELPERS_H */
