/*
 * bypass.c -- Bypass module registry implementation.
 *
 * Static array of registered modules. Selection iterates the list and
 * returns the first module whose probe() returns 1.
 */

#include <string.h>
#include "bypass/bypass.h"
#include "util/log.h"

static bypass_module_t g_modules[BYPASS_MAX_MODULES];
static int g_module_count = 0;

int bypass_register(const bypass_module_t *mod)
{
    if (!mod || !mod->probe || !mod->execute) {
        log_error("[bypass] Invalid module (missing callbacks)");
        return -1;
    }

    if (g_module_count >= BYPASS_MAX_MODULES) {
        log_error("[bypass] Module registry full (%d max)", BYPASS_MAX_MODULES);
        return -1;
    }

    memcpy(&g_modules[g_module_count], mod, sizeof(bypass_module_t));
    g_module_count++;

    log_info("[bypass] Registered module: %s", mod->name);
    return 0;
}

const bypass_module_t *bypass_select(device_info_t *dev)
{
    if (!dev) return NULL;

    for (int i = 0; i < g_module_count; i++) {
        int result = g_modules[i].probe(dev);
        if (result == 1) {
            log_info("[bypass] Selected module: %s", g_modules[i].name);
            return &g_modules[i];
        }
        if (result < 0) {
            log_warn("[bypass] probe() failed for '%s'", g_modules[i].name);
        }
    }

    return NULL;
}

int bypass_execute(const bypass_module_t *mod, device_info_t *dev)
{
    if (!mod) {
        log_error("[bypass] No module selected");
        return -1;
    }
    if (!dev) {
        log_error("[bypass] No device");
        return -1;
    }

    log_info("[bypass] Executing module: %s", mod->name);
    return mod->execute(dev);
}

void bypass_list(void)
{
    log_info("[bypass] %d registered module(s):", g_module_count);
    for (int i = 0; i < g_module_count; i++) {
        log_info("  [%d] %s -- %s", i, g_modules[i].name,
                 g_modules[i].description);
    }
}

int bypass_count(void)
{
    return g_module_count;
}
