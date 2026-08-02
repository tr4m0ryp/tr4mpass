#include <stdio.h>
#include <unistd.h>
#include "util/usb_helpers.h"
#include "util/log.h"

/* Maximum retry attempts for transient USB errors (PIPE/STALL) */
#define USB_PIPE_MAX_RETRIES  3

/* Delay between retries in microseconds (50ms) */
#define USB_PIPE_RETRY_DELAY  50000

/*
 * is_transient_usb_error -- Returns 1 if the libusb error code
 * represents a transient condition that may succeed on retry.
 * LIBUSB_ERROR_PIPE (-9) means the device STALLed the endpoint,
 * which is common during DFU operations and often clears on retry.
 *
 * NOTE: LIBUSB_ERROR_TIMEOUT is intentionally NOT retried here.
 * For checkm8 stage 2/3 async operations, timeouts are the intended
 * abort mechanism and must return immediately (no added latency).
 * For normal DFU operations (5000ms timeout), a real timeout means
 * the device is unresponsive -- retrying won't help.
 */
static int is_transient_usb_error(int err)
{
    return (err == LIBUSB_ERROR_PIPE);
}

int usb_ctrl_transfer(libusb_device_handle *dev,
                      uint8_t bmRequestType,
                      uint8_t bRequest,
                      uint16_t wValue,
                      uint16_t wIndex,
                      unsigned char *data,
                      uint16_t wLength,
                      unsigned int timeout)
{
    int ret;
    int attempt;

    if (!dev)
        return LIBUSB_ERROR_INVALID_PARAM;

    for (attempt = 0; attempt < USB_PIPE_MAX_RETRIES; attempt++) {
        ret = libusb_control_transfer(dev, bmRequestType, bRequest,
                                      wValue, wIndex, data, wLength, timeout);
        if (ret >= 0)
            return ret;

        if (!is_transient_usb_error(ret))
            return ret;

        /* Transient error -- retry after brief delay */
        if (attempt < USB_PIPE_MAX_RETRIES - 1) {
            log_warn("[usb] transient error %s on attempt %d/%d, retrying...",
                     libusb_strerror(ret), attempt + 1, USB_PIPE_MAX_RETRIES);
            usleep(USB_PIPE_RETRY_DELAY);
        }
    }

    return ret;
}

int usb_ctrl_transfer_no_data(libusb_device_handle *dev,
                              uint8_t bmRequestType,
                              uint8_t bRequest,
                              uint16_t wValue,
                              uint16_t wIndex,
                              unsigned int timeout)
{
    int ret;
    int attempt;

    if (!dev)
        return LIBUSB_ERROR_INVALID_PARAM;

    for (attempt = 0; attempt < USB_PIPE_MAX_RETRIES; attempt++) {
        ret = libusb_control_transfer(dev, bmRequestType, bRequest,
                                      wValue, wIndex, NULL, 0, timeout);
        if (ret >= 0)
            return 0;

        if (!is_transient_usb_error(ret))
            return ret;

        if (attempt < USB_PIPE_MAX_RETRIES - 1) {
            log_warn("[usb] transient error %s on attempt %d/%d, retrying...",
                     libusb_strerror(ret), attempt + 1, USB_PIPE_MAX_RETRIES);
            usleep(USB_PIPE_RETRY_DELAY);
        }
    }

    return ret;
}

void usb_print_error(int libusb_error)
{
    fprintf(stderr, "[usb] error %d: %s\n",
            libusb_error, libusb_strerror((enum libusb_error)libusb_error));

    if (libusb_error == LIBUSB_ERROR_PIPE) {
        fprintf(stderr, "[usb] PIPE error: the device STALLed the transfer.\n"
                "[usb] Troubleshooting:\n"
                "[usb]   1. Re-enter DFU mode and try again\n"
                "[usb]   2. Try a different USB port (prefer direct, "
                "not a hub)\n"
                "[usb]   3. Try a different USB cable (original Apple "
                "cable recommended)\n"
                "[usb]   4. On WSL/Linux: ensure usbipd or usbmuxd is "
                "running\n"
                "[usb]   5. On macOS: native USB tends to be more "
                "reliable\n");
    }
}
