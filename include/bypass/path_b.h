/*
 * path_b.h -- Path B bypass module for A12+ devices.
 *
 * Chains: DFU identity manipulation -> signal detection ->
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
 * path_b_manipulate_identity -- Modify device serial descriptors in DFU.
 * Appends PWND:[checkm8] marker to the device serial string via USB
 * control transfers to the DFU interface. Required before session
 * activation on A12+ devices.
 * Returns 0 on success, -1 on error.
 */
int path_b_manipulate_identity(device_info_t *dev);

/*
 * path_b_read_serial -- Read current serial descriptor from DFU mode.
 * Copies the serial string into buf (up to len bytes including NUL).
 * Returns 0 on success, -1 on error.
 */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len);

/*
 * path_b_write_serial -- Write a modified serial descriptor to the device.
 * Routes through path_b_write_serial_irecovery() which uses libirecovery
 * setenv commands from recovery mode (SET_DESCRIPTOR is rejected by A12+ BootROM).
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

/*
 * path_b_backfill_ids_via_irecv -- Populate dev->cpid/dev->ecid from libirecovery
 * when libusb DFU detection left them at zero. No-op when both are set.
 * Returns 0 on success (including already populated), -1 on failure.
 */
int path_b_backfill_ids_via_irecv(device_info_t *dev);

#endif /* PATH_B_H */
