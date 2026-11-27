/*
 * kis_dfu.h -- KIS-carried DFU identity probing.
 */

#ifndef KIS_DFU_H
#define KIS_DFU_H

#include "device/device.h"

/*
 * Detect an Apple device exposing physical USB PID 0x1881 with the
 * underlying DFU mode 0x1227.  On success, dev contains the copied
 * identity and transport metadata while dev->usb remains NULL.
 *
 * Returns 0 on success and -1 when no matching KIS device is present.
 */
int kis_dfu_detect(device_info_t *dev);

/*
 * Request one reboot from the previously identified KIS device.  The
 * device is opened by exact ECID and its KIS/DFU identity is reverified
 * before irecv_reboot() is called exactly once.
 *
 * Returns 0 on success, a negative irecovery status on failure, or -1
 * when the request is rejected before opening a device.
 */
int kis_dfu_request_reboot(device_info_t *dev);

#endif /* KIS_DFU_H */
