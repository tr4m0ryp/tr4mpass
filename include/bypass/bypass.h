/*
 * bypass.h -- Bypass module interface.
 *
 * Defines a function-pointer-based plugin architecture for bypass strategies.
 * Each module implements probe() to check compatibility and execute() to run.
 * Modules are registered at startup and selected based on device properties.
 */

#ifndef BYPASS_H
#define BYPASS_H

#include "device/device.h"

#define BYPASS_MAX_MODULES  16
#define BYPASS_NAME_LEN     64
#define BYPASS_DESC_LEN     256

typedef struct bypass_module {
    char name[BYPASS_NAME_LEN];
    char description[BYPASS_DESC_LEN];

    /*
     * probe -- Check if this module can handle the given device.
     * Returns 1 if compatible, 0 if not, -1 on error.
     */
    int (*probe)(device_info_t *dev);

    /*
     * execute -- Run the bypass on the given device.
     * Returns 0 on success, non-zero on failure.
     */
    int (*execute)(device_info_t *dev);
} bypass_module_t;

/*
 * bypass_register -- Register a bypass module.
 * Returns 0 on success, -1 if the registry is full.
 */
int bypass_register(const bypass_module_t *mod);

/*
 * bypass_select -- Find the first registered module whose probe() returns 1.
 * Returns a pointer to the module, or NULL if none match.
 */
const bypass_module_t *bypass_select(device_info_t *dev);

/*
 * bypass_execute -- Run the given module's execute() function.
 * Returns the module's return code, or -1 if mod is NULL.
 */
int bypass_execute(const bypass_module_t *mod, device_info_t *dev);

/*
 * bypass_list -- Print all registered modules to stdout.
 */
void bypass_list(void);

/*
 * bypass_count -- Return the number of registered modules.
 */
int bypass_count(void);

#endif /* BYPASS_H */
