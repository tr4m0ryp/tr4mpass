/*
 * recovery.h -- Exact-ECID Recovery-mode device detection.
 */

#ifndef DEVICE_RECOVERY_H
#define DEVICE_RECOVERY_H

#include <stdint.h>

#include "device/device.h"

/*
 * Open the requested ECID and accept it only when libirecovery confirms
 * a real Recovery mode (0x1280..0x1283). Device identity fields are
 * populated only after the ECID and mode checks both succeed.
 */
int recovery_detect_exact(device_info_t *dev, uint64_t expected_ecid);

#endif /* DEVICE_RECOVERY_H */
