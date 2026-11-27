/*
 * device_transport.h -- Transport classification for Apple USB devices.
 */

#ifndef DEVICE_TRANSPORT_H
#define DEVICE_TRANSPORT_H

#include <stdint.h>

typedef enum {
    DEVICE_TRANSPORT_NORMAL = 0,
    DEVICE_TRANSPORT_CLASSIC_DFU,
    DEVICE_TRANSPORT_KIS_DFU,
    DEVICE_TRANSPORT_RECOVERY
} device_transport_t;

/* Classify a physical USB product ID. */
device_transport_t device_transport_from_usb_pid(uint16_t pid);

/* Return a stable human-readable transport name. */
const char *device_transport_name(device_transport_t transport);

/* Return 1 only for actual iBoot Recovery modes. */
int device_transport_is_recovery_mode(int mode);

/* Return 1 only for transports that support classic raw-DFU Path A. */
int device_transport_allows_path_a(device_transport_t transport);

#endif /* DEVICE_TRANSPORT_H */
