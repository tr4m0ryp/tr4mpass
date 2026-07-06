/*
 * path_b_bootimg.h -- Resolve signed iBSS for Path B DFU -> recovery boot.
 *
 * Checks TR4MPASS_IBSS_PATH, TR4MPASS_IPSW_PATH, and common firmware
 * directories before falling back to manual recovery entry.
 */

#ifndef PATH_B_BOOTIMG_H
#define PATH_B_BOOTIMG_H

#include <stddef.h>
#include <stdint.h>

#include "device/device.h"

typedef enum {
    PATH_B_BOOTIMG_NONE = 0,
    PATH_B_BOOTIMG_DIRECT,
    PATH_B_BOOTIMG_FROM_IPSW,
    PATH_B_BOOTIMG_AUTODETECT
} path_b_bootimg_source_t;

typedef struct {
    char                    path[512];
    path_b_bootimg_source_t source;
} path_b_bootimg_result_t;

/*
 * path_b_resolve_ibss -- Resolve a readable iBSS path for the device.
 *
 * Returns  0 when out->path is set (source describes how it was found).
 * Returns  1 when no boot image is available (manual recovery fallback).
 * Returns -1 on a hard error (e.g. IPSW present but extraction failed).
 */
int path_b_resolve_ibss(const device_info_t *dev, path_b_bootimg_result_t *out);

/*
 * path_b_bootimg_source_name -- Human-readable label for logging.
 */
const char *path_b_bootimg_source_name(path_b_bootimg_source_t source);

#endif /* PATH_B_BOOTIMG_H */
