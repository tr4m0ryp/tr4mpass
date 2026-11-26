/*
 * path_b.h -- Path B bypass module for A12+ devices.
 *
 * Chains: Recovery identity manipulation -> signal detection ->
 * drmHandshake session activation -> deletescript cleanup -> verify.
 * Does NOT use a BootROM exploit -- works through identity
 * manipulation and the session-mode activation protocol.
 */

#ifndef PATH_B_H
#define PATH_B_H

#include "bypass/bypass.h"

/* The Path B bypass module for registration with bypass_register(). */
extern const bypass_module_t path_b_module;

/*
 * path_b_manipulate_identity -- Modify the Recovery identity environment.
 * Appends the PWND:[checkm8] marker to the iBoot serial-number value.
 * Required before session activation on A12+ devices.
 * Returns 0 on success, -1 on error.
 */
int path_b_manipulate_identity(device_info_t *dev);

/*
 * path_b_read_serial -- Read the current identity through libirecovery.
 * Copies the serial string into buf (up to len bytes including NUL).
 * Returns 0 on success, -1 on error.
 */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len);

/*
 * path_b_write_serial -- Write a modified identity to the iBoot environment.
 * Uses libirecovery setenv/saveenv from Recovery mode.
 * Returns 0 on success, -1 on error.
 */
int path_b_write_serial(device_info_t *dev, const char *new_serial);

/*
 * path_b_write_serial_irecovery -- Write serial via iRecovery setenv in recovery mode.
 * Device must already be in recovery mode (PID 0x1281) before calling.
 * Used internally by path_b.c step_manipulate_identity().
 * Returns 0 on success, -1 on error.
 */
int path_b_write_serial_irecovery(device_info_t *dev, const char *new_serial);

#endif /* PATH_B_H */
