/*
 * path_b_recovery.h -- DFU to recovery transition for Path B (A12+).
 */

#ifndef PATH_B_RECOVERY_H
#define PATH_B_RECOVERY_H

#include "device/device.h"

/*
 * path_b_reboot_to_recovery -- Boot or wait for recovery mode (PID 0x128x).
 *
 * Phase 1: automatic entry via signed iBSS upload (when available) or
 * irecv USB reset, polling up to 60s without manual instructions.
 * Phase 2: if still not in recovery, prints manual entry instructions
 * and continues polling until 120s total.
 *
 * On success dev->usb is NULL and dev->is_dfu_mode is 0.
 * Returns 0 on success, -1 on error/timeout.
 */
int path_b_reboot_to_recovery(device_info_t *dev, const char *ibss_path);

#endif /* PATH_B_RECOVERY_H */
