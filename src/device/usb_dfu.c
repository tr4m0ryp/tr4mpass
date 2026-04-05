/* usb_dfu.c -- Apple DFU mode device detection and raw USB I/O (libusb) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "device/usb_dfu.h"
#include "util/usb_helpers.h"
#include "util/log.h"

/* DFU control transfer direction flags */
#define DFU_REQUEST_OUT  (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)
#define DFU_REQUEST_IN   (LIBUSB_ENDPOINT_IN  | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)

/* Maximum chunk size for a single DFU transfer */
#define DFU_MAX_TRANSFER 0x800

/* Serial string descriptor index for Apple DFU devices */
#define DFU_SERIAL_INDEX 3

/* Module-global libusb context */
static libusb_context *g_ctx = NULL;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int usb_dfu_init(void)
{
    int ret;

    if (g_ctx) {
        log_warn("usb_dfu_init: libusb context already initialized");
        return 0;
    }

    ret = libusb_init(&g_ctx);
    if (ret != LIBUSB_SUCCESS) {
        log_error("libusb_init failed: %s", libusb_strerror(ret));
        return -1;
    }

#if LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(g_ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
#else
    libusb_set_debug(g_ctx, LIBUSB_LOG_LEVEL_WARNING);
#endif

    log_debug("libusb context initialized");
    return 0;
}

void usb_dfu_cleanup(void)
{
    if (g_ctx) {
        libusb_exit(g_ctx);
        g_ctx = NULL;
        log_debug("libusb context cleaned up");
    }
}

/* ------------------------------------------------------------------ */
/* Device discovery                                                    */
/* ------------------------------------------------------------------ */

int usb_dfu_find(libusb_device_handle **handle)
{
    libusb_device **devs = NULL;
    ssize_t count;
    ssize_t i;
    int found = 0;

    if (!handle)
        return -1;
    *handle = NULL;

    if (!g_ctx) {
        log_error("usb_dfu_find: libusb not initialized (call usb_dfu_init first)");
        return -1;
    }

    count = libusb_get_device_list(g_ctx, &devs);
    if (count < 0) {
        log_error("libusb_get_device_list failed: %s",
                  libusb_strerror((int)count));
        return -1;
    }

    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(devs[i], &desc);
        if (ret != LIBUSB_SUCCESS)
            continue;

        if (desc.idVendor == APPLE_VID && desc.idProduct == DFU_PID) {
            ret = libusb_open(devs[i], handle);
            if (ret != LIBUSB_SUCCESS) {
                log_error("failed to open DFU device: %s",
                          libusb_strerror(ret));
                *handle = NULL;
                continue;
            }
            log_info("DFU device found (bus %d, addr %d)",
                     libusb_get_bus_number(devs[i]),
                     libusb_get_device_address(devs[i]));
            found = 1;
            break;
        }
    }

    libusb_free_device_list(devs, 1);

    if (!found) {
        log_debug("no Apple DFU device found");
        return -1;
    }

    libusb_detach_kernel_driver(*handle, 0);  /* Linux: detach kernel driver; ignore error */

    /* Claim interface 0 (DFU interface) */
    int ret = libusb_claim_interface(*handle, 0);
    if (ret != LIBUSB_SUCCESS)
        log_warn("failed to claim interface 0: %s (continuing anyway)", libusb_strerror(ret));

    return 0;
}

/* ------------------------------------------------------------------ */
/* Serial string parsing                                               */
/* ------------------------------------------------------------------ */

/*
 * Parse a hex value following a key prefix in the serial string.
 * Example: parse_hex_field("CPID:8015 ECID:...", "CPID:", &val)
 * Returns 0 on success, -1 if the key is not found.
 */
static int parse_hex_field(const char *serial, const char *key, uint64_t *out)
{
    const char *p;
    char *endptr;

    if (!serial || !key || !out)
        return -1;
    p = strstr(serial, key);
    if (!p)
        return -1;
    endptr = NULL;
    *out = strtoull(p + strlen(key), &endptr, 16);
    if (!endptr || endptr == p + strlen(key)) {
        log_warn("parse_hex_field: garbage value for key '%s'", key);
        *out = 0;
    }
    return 0;
}

int usb_dfu_read_info(libusb_device_handle *handle, uint32_t *cpid,
                      uint64_t *ecid, char *serial, size_t serial_len)
{
    unsigned char buf[DFU_SERIAL_MAX];
    int ret;
    uint64_t val;

    if (!handle)
        return -1;

    /* Initialize outputs to safe defaults */
    if (cpid) *cpid = 0;
    if (ecid) *ecid = 0;
    if (serial && serial_len > 0) serial[0] = '\0';

    /* Read the serial string descriptor (index 3 for Apple DFU) */
    ret = libusb_get_string_descriptor_ascii(handle, DFU_SERIAL_INDEX,
                                             buf, sizeof(buf));
    if (ret < 0) {
        log_error("failed to read serial descriptor: %s",
                  libusb_strerror(ret));
        return -1;
    }

    if (ret >= (int)sizeof(buf))
        ret = (int)sizeof(buf) - 1;
    buf[ret] = '\0';
    log_debug("DFU serial string: %s", (char *)buf);

    /* Copy full serial string to caller */
    if (serial && serial_len > 0) {
        strncpy(serial, (char *)buf, serial_len - 1);
        serial[serial_len - 1] = '\0';
    }

    /* Parse CPID */
    if (cpid) {
        if (parse_hex_field((char *)buf, "CPID:", &val) == 0) {
            *cpid = (uint32_t)val;
            log_info("CPID: 0x%04X", *cpid);
        } else {
            log_warn("CPID field not found in serial string");
        }
    }

    /* Parse ECID */
    if (ecid) {
        if (parse_hex_field((char *)buf, "ECID:", &val) == 0) {
            *ecid = val;
            log_info("ECID: 0x%016" PRIX64, *ecid);
        } else {
            log_warn("ECID field not found in serial string");
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Data transfer                                                       */
/* ------------------------------------------------------------------ */

int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len)
{
    size_t sent = 0;
    uint16_t block_num = 0;
    int ret;

    if (!handle || (!data && len > 0))
        return -1;

    /* DFU DNLOAD: send in DFU_MAX_TRANSFER chunks; wValue is block number (DFU spec). */
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > DFU_MAX_TRANSFER)
            chunk = DFU_MAX_TRANSFER;

        ret = usb_ctrl_transfer(handle, DFU_REQUEST_OUT, DFU_DNLOAD,
                                block_num, 0, (unsigned char *)data + sent,
                                (uint16_t)chunk, DFU_USB_TIMEOUT);
        if (ret < 0) {
            log_error("DFU DNLOAD failed at offset %zu: %s",
                      sent, libusb_strerror(ret));
            usb_print_error(ret);
            return -1;
        }

        sent += (size_t)ret;
        log_debug("DFU DNLOAD block %u: sent %zu / %zu bytes", (unsigned)block_num, sent, len);
        block_num++;
    }

    return 0;
}

int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len,
                 size_t *actual)
{
    int ret;
    uint16_t xfer_len;

    if (!handle || !buf || !actual)
        return -1;

    *actual = 0;

    if (len > DFU_MAX_TRANSFER)
        xfer_len = DFU_MAX_TRANSFER;
    else
        xfer_len = (uint16_t)len;

    /*
     * DFU UPLOAD: bmRequestType = device-to-host, class, interface
     * bRequest = DFU_UPLOAD (2), wValue = block number, wIndex = 0
     */
    ret = usb_ctrl_transfer(handle, DFU_REQUEST_IN, DFU_UPLOAD,
                            0, 0, (unsigned char *)buf,
                            xfer_len, DFU_USB_TIMEOUT);
    if (ret < 0) {
        log_error("DFU UPLOAD failed: %s", libusb_strerror(ret));
        usb_print_error(ret);
        return -1;
    }

    *actual = (size_t)ret;
    log_debug("DFU UPLOAD: received %zu bytes", *actual);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */

void usb_dfu_close(libusb_device_handle *handle)
{
    if (!handle)
        return;

    libusb_release_interface(handle, 0);
    libusb_close(handle);
    log_debug("DFU device handle closed");
}
