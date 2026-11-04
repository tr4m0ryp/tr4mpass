/*
 * path_a_internal.h -- Internal declarations shared between path_a.c
 * and path_a_deliver.c.
 *
 * Not part of the public API. Include only within the pipeline module.
 */

#ifndef PIPELINE_PATH_A_INTERNAL_H
#define PIPELINE_PATH_A_INTERNAL_H

#include "device/device.h"

int path_a_load_ramdisk(device_info_t *dev);
int path_a_deliver(device_info_t *dev);
int path_a_replace_mobileactivationd(device_info_t *dev);

#endif /* PIPELINE_PATH_A_INTERNAL_H */
