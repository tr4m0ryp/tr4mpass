/*
 * pipeline.c -- Pipeline module registry implementation.
 *
 * Static array of registered modules. Selection iterates the list and
 * returns the first module whose probe() returns 1.
 */

#include <string.h>
#include "pipeline/pipeline.h"
#include "util/log.h"

static pipeline_module_t g_modules[PIPELINE_MAX_MODULES];
static int g_module_count = 0;

int pipeline_register(const pipeline_module_t *mod)
{
    if (!mod || !mod->probe || !mod->execute) {
        log_error("[pipeline] Invalid module (missing callbacks)");
        return -1;
    }

    if (g_module_count >= PIPELINE_MAX_MODULES) {
        log_error("[pipeline] Module registry full (%d max)", PIPELINE_MAX_MODULES);
        return -1;
    }

    memcpy(&g_modules[g_module_count], mod, sizeof(pipeline_module_t));
    g_module_count++;

    log_info("[pipeline] Registered module: %s", mod->name);
    return 0;
}

const pipeline_module_t *pipeline_select(device_info_t *dev)
{
    if (!dev) return NULL;

    for (int i = 0; i < g_module_count; i++) {
        int result = g_modules[i].probe(dev);
        if (result == 1) {
            log_info("[pipeline] Selected module: %s", g_modules[i].name);
            return &g_modules[i];
        }
        if (result < 0) {
            log_warn("[pipeline] probe() failed for '%s'", g_modules[i].name);
        }
    }

    return NULL;
}

int pipeline_execute(const pipeline_module_t *mod, device_info_t *dev)
{
    if (!mod) {
        log_error("[pipeline] No module selected");
        return -1;
    }
    if (!dev) {
        log_error("[pipeline] No device");
        return -1;
    }

    log_info("[pipeline] Executing module: %s", mod->name);
    return mod->execute(dev);
}

void pipeline_list(void)
{
    log_info("[pipeline] %d registered module(s):", g_module_count);
    for (int i = 0; i < g_module_count; i++) {
        log_info("  [%d] %s -- %s", i, g_modules[i].name,
                 g_modules[i].description);
    }
}

int pipeline_count(void)
{
    return g_module_count;
}
