/*
 * kis_dfu.c -- libirecovery-backed KIS-carried DFU detection.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libirecovery.h>

#include "device/device.h"
#include "device/kis_dfu.h"
#include "device/usb_dfu.h"
#include "util/log.h"

#define KIS_OPEN_ATTEMPTS 3

static void close_client(irecv_client_t *client)
{
    if (client && *client) {
        irecv_close(*client);
        *client = NULL;
    }
}

static int read_kis_identity(irecv_client_t client, int *mode,
                             const struct irecv_device_info **info)
{
    irecv_error_t error;

    if (!client || !mode || !info)
        return -1;

    *mode = 0;
    *info = NULL;

    error = irecv_get_mode(client, mode);
    if (error != IRECV_E_SUCCESS) {
        log_debug("KIS probe: irecv_get_mode failed: %s",
                  irecv_strerror(error));
        return -1;
    }

    *info = irecv_get_device_info(client);
    if (!*info) {
        log_debug("KIS probe: irecv_get_device_info returned no identity");
        return -1;
    }

    return 0;
}

int kis_dfu_detect(device_info_t *dev)
{
    irecv_client_t client = NULL;
    const struct irecv_device_info *info = NULL;
    int mode = 0;
    irecv_error_t error;

    if (!dev)
        return -1;

    /* The ECID is unknown until the KIS identity block has been read. */
    error = irecv_open_with_ecid_and_attempts(&client, 0,
                                               KIS_OPEN_ATTEMPTS);
    if (error != IRECV_E_SUCCESS) {
        log_debug("KIS probe: open failed: %s", irecv_strerror(error));
        return -1;
    }

    if (read_kis_identity(client, &mode, &info) < 0 ||
        info->pid != KIS_PID || mode != DFU_PID) {
        log_debug("KIS probe: non-matching identity (pid=0x%04x, mode=0x%04x)",
                  info ? info->pid : 0, mode);
        close_client(&client);
        return -1;
    }

    dev->cpid = info->cpid;
    dev->ecid = info->ecid;
    snprintf(dev->serial, sizeof(dev->serial), "%s",
             info->serial_string ? info->serial_string : "");
    dev->is_dfu_mode = 1;
    dev->usb = NULL;
    dev->iserial_index = 0;
    dev->transport = DEVICE_TRANSPORT_KIS_DFU;
    dev->usb_product_id = info->pid;
    dev->boot_mode = (uint16_t)mode;

    log_info("KIS DFU device found (PID 0x%04X, underlying mode 0x%04X)",
             dev->usb_product_id, dev->boot_mode);
    close_client(&client);
    return 0;
}

int kis_dfu_request_reboot(device_info_t *dev)
{
    irecv_client_t client = NULL;
    const struct irecv_device_info *info = NULL;
    int mode = 0;
    irecv_error_t error;

    if (!dev || dev->ecid == 0) {
        log_error("KIS reboot refused: no exact ECID");
        return -1;
    }

    error = irecv_open_with_ecid_and_attempts(&client, dev->ecid,
                                               KIS_OPEN_ATTEMPTS);
    if (error != IRECV_E_SUCCESS) {
        log_debug("KIS reboot: exact-ECID open failed: %s",
                  irecv_strerror(error));
        return -1;
    }

    if (read_kis_identity(client, &mode, &info) < 0 ||
        info->pid != KIS_PID || mode != DFU_PID ||
        info->ecid != dev->ecid) {
        log_debug("KIS reboot: opened identity does not match (pid=0x%04x, "
                  "mode=0x%04x, ecid=0x%016llx)",
                  info ? info->pid : 0, mode,
                  info ? (unsigned long long)info->ecid : 0ULL);
        close_client(&client);
        return -1;
    }

    error = irecv_reboot(client);
    close_client(&client);
    return (int)error;
}

