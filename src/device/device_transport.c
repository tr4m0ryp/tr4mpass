/* device_transport.c -- Apple USB transport classification. */

#include "device/device_transport.h"

#define CLASSIC_DFU_PID       0x1227u
#define KIS_DFU_PID           0x1881u
#define RECOVERY_PID_MIN      0x1280u
#define RECOVERY_PID_MAX      0x1283u

device_transport_t device_transport_from_usb_pid(uint16_t pid)
{
    if (pid == CLASSIC_DFU_PID)
        return DEVICE_TRANSPORT_CLASSIC_DFU;
    if (pid == KIS_DFU_PID)
        return DEVICE_TRANSPORT_KIS_DFU;
    if (pid >= RECOVERY_PID_MIN && pid <= RECOVERY_PID_MAX)
        return DEVICE_TRANSPORT_RECOVERY;
    return DEVICE_TRANSPORT_NORMAL;
}

const char *device_transport_name(device_transport_t transport)
{
    switch (transport) {
    case DEVICE_TRANSPORT_CLASSIC_DFU:
        return "Classic DFU";
    case DEVICE_TRANSPORT_KIS_DFU:
        return "KIS DFU (Debug USB)";
    case DEVICE_TRANSPORT_RECOVERY:
        return "Recovery";
    case DEVICE_TRANSPORT_NORMAL:
    default:
        return "Normal";
    }
}

int device_transport_is_recovery_mode(int mode)
{
    return mode >= (int)RECOVERY_PID_MIN && mode <= (int)RECOVERY_PID_MAX;
}

int device_transport_allows_path_a(device_transport_t transport)
{
    return transport == DEVICE_TRANSPORT_CLASSIC_DFU;
}
