/*
 * recovery.c -- libirecovery-backed exact-ECID Recovery detection.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libirecovery.h>

#include "device/device.h"
#include "device/device_transport.h"
#include "device/recovery.h"
#include "util/log.h"

#define RECOVERY_OPEN_ATTEMPTS 3

int recovery_detect_exact(device_info_t *dev, uint64_t expected_ecid)
{
    irecv_client_t client = NULL;
    const struct irecv_device_info *info;
    irecv_error_t error;
    int mode = 0;
    uint16_t physical_pid;
    uint32_t cpid;
    uint64_t ecid;
    char serial[DEVICE_FIELD_LEN];

    if (!dev || expected_ecid == 0) {
        log_error("Recovery detection requires an exact ECID");
        return -1;
    }

    error = irecv_open_with_ecid_and_attempts(&client, expected_ecid,
                                               RECOVERY_OPEN_ATTEMPTS);
    if (error != IRECV_E_SUCCESS || !client) {
        log_error("Recovery exact-ECID open failed: %s", irecv_strerror(error));
        return -1;
    }

    error = irecv_get_mode(client, &mode);
    if (error != IRECV_E_SUCCESS ||
        !device_transport_is_recovery_mode(mode)) {
        log_error("Device is not in Recovery mode (mode=0x%04X, err=%s)",
                  (unsigned)mode, irecv_strerror(error));
        irecv_close(client);
        return -1;
    }

    info = irecv_get_device_info(client);
    if (!info || info->ecid != expected_ecid) {
        log_error("Recovery identity ECID mismatch (expected=0x%016llx, "
                  "actual=0x%016llx)",
                  (unsigned long long)expected_ecid,
                  (unsigned long long)(info ? info->ecid : 0ULL));
        irecv_close(client);
        return -1;
    }

    physical_pid = info->pid ? info->pid : (uint16_t)mode;
    cpid = info->cpid;
    ecid = info->ecid;
    snprintf(serial, sizeof(serial), "%s",
             info->serial_string ? info->serial_string : "");

    dev->transport = DEVICE_TRANSPORT_RECOVERY;
    dev->usb_product_id = physical_pid;
    dev->boot_mode = (uint16_t)mode;
    dev->cpid = cpid;
    dev->ecid = ecid;
    snprintf(dev->serial, sizeof(dev->serial), "%s", serial);
    dev->is_dfu_mode = 0;
    dev->usb = NULL;

    log_info("Recovery device found (PID 0x%04X, mode 0x%04X, ECID 0x%llX)",
             physical_pid, (unsigned)mode, (unsigned long long)ecid);
    irecv_close(client);
    return 0;
}
