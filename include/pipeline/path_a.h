/*
 * path_a.h -- Path A pipeline module for A5-A11 devices.
 *
 * Chains: checkm8 BootROM stage -> ramdisk -> delivery ->
 * activation record submission -> deletescript cleanup -> verify.
 */

#ifndef PATH_A_H
#define PATH_A_H

#include "pipeline/pipeline.h"

/* The Path A pipeline module for registration with pipeline_register(). */
extern const pipeline_module_t path_a_module;

#endif /* PATH_A_H */
