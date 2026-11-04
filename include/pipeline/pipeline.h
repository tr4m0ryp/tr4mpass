/*
 * pipeline.h -- Pipeline module interface.
 *
 * Defines a function-pointer-based plugin architecture for pipeline strategies.
 * Each module implements probe() to check compatibility and execute() to run.
 * Modules are registered at startup and selected based on device properties.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include "device/device.h"

#define PIPELINE_MAX_MODULES  16
#define PIPELINE_NAME_LEN     64
#define PIPELINE_DESC_LEN     256

typedef struct pipeline_module {
    char name[PIPELINE_NAME_LEN];
    char description[PIPELINE_DESC_LEN];

    /*
     * probe -- Check if this module can handle the given device.
     * Returns 1 if compatible, 0 if not, -1 on error.
     */
    int (*probe)(device_info_t *dev);

    /*
     * execute -- Run the pipeline on the given device.
     * Returns 0 on success, non-zero on failure.
     */
    int (*execute)(device_info_t *dev);
} pipeline_module_t;

/*
 * pipeline_register -- Register a pipeline module.
 * Returns 0 on success, -1 if the registry is full.
 */
int pipeline_register(const pipeline_module_t *mod);

/*
 * pipeline_select -- Find the first registered module whose probe() returns 1.
 * Returns a pointer to the module, or NULL if none match.
 */
const pipeline_module_t *pipeline_select(device_info_t *dev);

/*
 * pipeline_execute -- Run the given module's execute() function.
 * Returns the module's return code, or -1 if mod is NULL.
 */
int pipeline_execute(const pipeline_module_t *mod, device_info_t *dev);

/*
 * pipeline_list -- Print all registered modules to stdout.
 */
void pipeline_list(void);

/*
 * pipeline_count -- Return the number of registered modules.
 */
int pipeline_count(void);

#endif /* PIPELINE_H */
