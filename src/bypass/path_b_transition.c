/*
 * path_b_transition.c -- Transport-aware DFU-to-Recovery transition.
 */

#include <stdint.h>
#include <unistd.h>

#include <libirecovery.h>
#include <libusb.h>

#include "bypass/path_b_transition.h"
#include "device/kis_dfu.h"
#include "device/usb_dfu.h"
#include "util/log.h"

#define DFU_ABORT_TIMEOUT_MS 1000

static void print_face_id_recovery_fallback(void)
{
    log_warn("[path_b] The experimental KIS reboot request did not succeed.");
    log_info("[path_b] Physical Recovery fallback for Face ID devices:");
    log_info("  1. Quickly press and release Volume Up.");
    log_info("  2. Quickly press and release Volume Down.");
    log_info("  3. Hold Side until the \"Connect to computer\" screen appears.");
}

static int poll_for_recovery(device_info_t *dev,
                             unsigned int timeout_secs,
                             unsigned int poll_secs,
                             const char *transport_name)
{
    unsigned int elapsed = 0;
    unsigned int interval = poll_secs ? poll_secs : 1;

    while (elapsed < timeout_secs) {
        irecv_client_t client = NULL;
        irecv_error_t error;
        int mode = 0;

        if (poll_secs)
            sleep(poll_secs);

        error = irecv_open_with_ecid(&client, dev->ecid);
        if (error == IRECV_E_SUCCESS && client) {
            error = irecv_get_mode(client, &mode);
            if (error == IRECV_E_SUCCESS &&
                device_transport_is_recovery_mode(mode)) {
                irecv_close(client);
                dev->transport = DEVICE_TRANSPORT_RECOVERY;
                dev->usb_product_id = (uint16_t)mode;
                dev->boot_mode = (uint16_t)mode;
                dev->is_dfu_mode = 0;
                log_info("[path_b] Recovery confirmed via libirecovery (mode 0x%04X)",
                         mode);
                return 0;
            }
            irecv_close(client);
        }

        elapsed += interval;
        log_debug("[path_b] Waiting for Recovery via %s (%us / %us)",
                  transport_name, elapsed, timeout_secs);
    }

    log_error("[path_b] %s transition timed out waiting for Recovery (%us)",
              transport_name, timeout_secs);
    return -1;
}

static int transition_kis(device_info_t *dev,
                           unsigned int timeout_secs,
                           unsigned int poll_secs)
{
    int request_status;

    if (!dev->experimental_kis_transition) {
        log_error("[path_b] KIS-to-Recovery requires "
                  "--experimental-kis-transition");
        return -1;
    }
    if (dev->ecid == 0) {
        log_error("[path_b] KIS-to-Recovery requires an exact ECID");
        return -1;
    }

    request_status = kis_dfu_request_reboot(dev);
    if (request_status != 0) {
        log_warn("[path_b] Experimental KIS reboot request failed (%d)",
                 request_status);
        print_face_id_recovery_fallback();
    }

    return poll_for_recovery(dev, timeout_secs, poll_secs, "KIS");
}

static int transition_classic_dfu(device_info_t *dev,
                                  unsigned int timeout_secs,
                                  unsigned int poll_secs)
{
    if (!dev->usb) {
        log_error("[path_b] Classic DFU transition requires a USB handle");
        return -1;
    }
    if (dev->ecid == 0) {
        log_error("[path_b] Classic DFU transition requires an exact ECID");
        return -1;
    }

    /* DFU_ABORT: class/interface host-to-device request 6. */
    libusb_control_transfer(dev->usb, 0x21, DFU_ABORT,
                            0, 0, NULL, 0, DFU_ABORT_TIMEOUT_MS);
    usb_dfu_close(dev->usb);
    dev->usb = NULL;
    dev->is_dfu_mode = 0;

    return poll_for_recovery(dev, timeout_secs, poll_secs, "classic DFU");
}

int path_b_transition_to_recovery(device_info_t *dev,
                                  unsigned int timeout_secs,
                                  unsigned int poll_secs)
{
    if (!dev)
        return -1;

    if (dev->transport == DEVICE_TRANSPORT_RECOVERY) {
        if (dev->ecid == 0) {
            log_error("[path_b] Recovery resume requires an exact ECID");
            return -1;
        }
        if (!device_transport_is_recovery_mode(dev->boot_mode)) {
            log_error("[path_b] Recovery resume requires confirmed Recovery "
                      "mode (mode=0x%04X)", (unsigned)dev->boot_mode);
            return -1;
        }
        log_info("[path_b] Recovery already confirmed -- skipping transition");
        return 0;
    }

    if (dev->transport == DEVICE_TRANSPORT_KIS_DFU)
        return transition_kis(dev, timeout_secs, poll_secs);
    if (dev->transport == DEVICE_TRANSPORT_CLASSIC_DFU)
        return transition_classic_dfu(dev, timeout_secs, poll_secs);

    log_error("[path_b] Unsupported DFU transport for Recovery transition: %s",
              device_transport_name(dev->transport));
    return -1;
}
