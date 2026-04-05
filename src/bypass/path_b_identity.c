/*
 * path_b_identity.c -- DFU identity manipulation for A12+ devices.
 *
 * Modifies the device serial string descriptor in DFU mode to append
 * the PWND:[checkm8] marker via USB control transfers. V1 uses correct
 * transfer structure; exact SRAM write offsets are TODO (hardware testing).
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

#include "bypass/path_b.h"
#include "device/usb_dfu.h"
#include "util/log.h"

/* USB descriptor request types */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_DESCRIPTOR  0x07
#define USB_DT_STRING           0x03

/* Serial string descriptor index for Apple DFU devices */
#define DFU_SERIAL_DESC_INDEX   3

/* PWND marker appended to the serial string */
#define PWND_MARKER             " PWND:[checkm8]"

/* Maximum USB string descriptor length per USB 2.0 spec */
#define USB_DESC_MAX_LEN        255

/*
 * usb_get_string_descriptor -- GET_DESCRIPTOR control transfer for a
 * USB string descriptor. Returns bytes received, or -1 on error.
 */
static int usb_get_string_descriptor(libusb_device_handle *usb,
                                     uint8_t index,
                                     uint8_t *buf, size_t len)
{
    int rc;

    if (!usb || !buf || len == 0) {
        log_error("[path_b_id] Invalid arguments to get_string_descriptor");
        return -1;
    }

    rc = libusb_control_transfer(
        usb,
        0x80,                                       /* bmRequestType  */
        USB_REQ_GET_DESCRIPTOR,                     /* bRequest       */
        (uint16_t)((USB_DT_STRING << 8) | index),  /* wValue         */
        0x0409,                                     /* wIndex (lang)  */
        buf,                                        /* data           */
        (uint16_t)(len < USB_DESC_MAX_LEN ? len : USB_DESC_MAX_LEN),
        DFU_USB_TIMEOUT                             /* timeout ms     */
    );

    if (rc < 0) {
        log_error("[path_b_id] GET_DESCRIPTOR failed: %s",
                  libusb_error_name(rc));
        return -1;
    }

    /* Validate minimum descriptor header */
    if (rc < 2 || buf[1] != USB_DT_STRING) {
        log_error("[path_b_id] Invalid string descriptor (len=%d, type=0x%02X)",
                  rc, rc >= 2 ? buf[1] : 0);
        return -1;
    }

    return rc;
}

/*
 * utf16le_to_ascii -- Convert USB string descriptor UTF-16LE to ASCII.
 * Skips the 2-byte header. Non-ASCII characters become '?'.
 */
static int utf16le_to_ascii(const uint8_t *desc, int desc_len,
                            char *out, size_t out_len)
{
    int     i;
    size_t  pos = 0;
    int     str_bytes;

    if (!desc || desc_len < 2 || !out || out_len == 0)
        return -1;

    /* String data starts at byte 2, each char is 2 bytes (UTF-16LE) */
    str_bytes = desc_len - 2;

    for (i = 0; i < str_bytes && pos < out_len - 1; i += 2) {
        uint8_t lo = desc[2 + i];
        uint8_t hi = (i + 1 < str_bytes) ? desc[2 + i + 1] : 0;

        if (hi == 0 && lo >= 0x20 && lo <= 0x7E)
            out[pos++] = (char)lo;
        else
            out[pos++] = '?';
    }

    out[pos] = '\0';
    return (int)pos;
}

/*
 * ascii_to_utf16le_desc -- Build USB string descriptor from ASCII.
 * Returns total descriptor length, or -1 on error.
 */
static int ascii_to_utf16le_desc(const char *ascii,
                                 uint8_t *desc, size_t desc_max)
{
    size_t  slen;
    size_t  total;
    size_t  i;

    if (!ascii || !desc || desc_max < 4)
        return -1;

    slen  = strlen(ascii);
    total = 2 + (slen * 2);

    if (total > desc_max || total > USB_DESC_MAX_LEN)
        return -1;

    desc[0] = (uint8_t)total;      /* bLength           */
    desc[1] = USB_DT_STRING;       /* bDescriptorType   */

    for (i = 0; i < slen; i++) {
        desc[2 + (i * 2)]     = (uint8_t)ascii[i];   /* low byte  */
        desc[2 + (i * 2) + 1] = 0x00;                /* high byte */
    }

    return (int)total;
}

/* path_b_read_serial -- Read serial descriptor from DFU, decode to ASCII. */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len)
{
    uint8_t desc[USB_DESC_MAX_LEN];
    int     desc_len;

    if (!dev || !buf || len == 0) {
        log_error("[path_b_id] Invalid arguments to read_serial");
        return -1;
    }

    if (!dev->usb) {
        log_error("[path_b_id] No USB handle -- device not in DFU mode?");
        return -1;
    }

    desc_len = usb_get_string_descriptor(dev->usb, DFU_SERIAL_DESC_INDEX,
                                         desc, sizeof(desc));
    if (desc_len < 0)
        return -1;

    if (utf16le_to_ascii(desc, desc_len, buf, len) < 0) {
        log_error("[path_b_id] Failed to decode serial descriptor");
        return -1;
    }

    log_debug("[path_b_id] Read serial: %s", buf);
    return 0;
}

/*
 * path_b_write_serial -- Write modified serial descriptor via SET_DESCRIPTOR.
 *
 * TODO: A12+ DFU serial resides in writable SRAM. The exact write offset
 * varies by chip (T8020/T8030/T8101/T8110/T8120/T8103/T8112) and must be
 * determined by hardware testing. May need vendor-specific control transfer
 * instead of standard SET_DESCRIPTOR if the DFU firmware rejects it.
 */
int path_b_write_serial(device_info_t *dev, const char *new_serial)
{
    uint8_t desc[USB_DESC_MAX_LEN];
    int     desc_len;
    int     rc;

    if (!dev || !new_serial) {
        log_error("[path_b_id] Invalid arguments to write_serial");
        return -1;
    }

    if (!dev->usb) {
        log_error("[path_b_id] No USB handle -- device not in DFU mode?");
        return -1;
    }

    desc_len = ascii_to_utf16le_desc(new_serial, desc, sizeof(desc));
    if (desc_len < 0) {
        log_error("[path_b_id] Serial string too long for USB descriptor");
        return -1;
    }

    /* SET_DESCRIPTOR: host-to-device, standard, device recipient */
    rc = libusb_control_transfer(
        dev->usb,
        0x00,                                               /* bmRequestType  */
        USB_REQ_SET_DESCRIPTOR,                             /* bRequest       */
        (uint16_t)((USB_DT_STRING << 8) | DFU_SERIAL_DESC_INDEX),
        0x0409,                                             /* wIndex (lang)  */
        desc,                                               /* data           */
        (uint16_t)desc_len,                                 /* wLength        */
        DFU_USB_TIMEOUT                                     /* timeout ms     */
    );

    if (rc < 0) {
        log_warn("[path_b_id] SET_DESCRIPTOR returned: %s",
                 libusb_error_name(rc));
        /* TODO: fallback vendor-specific transfer for A12+ DFU firmware */
        return -1;
    }

    log_info("[path_b_id] Serial descriptor written (%d bytes)", desc_len);
    return 0;
}

/* path_b_manipulate_identity -- Read, append PWND marker, write, verify. */
int path_b_manipulate_identity(device_info_t *dev)
{
    char current[DFU_SERIAL_MAX];
    char modified[DFU_SERIAL_MAX];
    char verify[DFU_SERIAL_MAX];
    int  rc;

    if (!dev) {
        log_error("[path_b_id] NULL device");
        return -1;
    }

    if (!dev->usb) {
        log_error("[path_b_id] No USB handle for DFU mode");
        return -1;
    }

    /* Step 1: read current serial */
    log_info("[path_b_id] Reading current serial descriptor...");
    rc = path_b_read_serial(dev, current, sizeof(current));
    if (rc != 0) {
        log_error("[path_b_id] Cannot read serial descriptor");
        return -1;
    }
    log_info("[path_b_id] Current serial: %s", current);

    /* Step 2: check if already manipulated */
    if (strstr(current, "PWND:") != NULL) {
        log_info("[path_b_id] PWND marker already present -- skipping");
        return 0;
    }

    /* Step 3: build modified serial with PWND marker */
    rc = snprintf(modified, sizeof(modified), "%s%s", current, PWND_MARKER);
    if (rc < 0 || (size_t)rc >= sizeof(modified)) {
        log_error("[path_b_id] Modified serial exceeds buffer size");
        return -1;
    }
    log_info("[path_b_id] Modified serial: %s", modified);

    /* Step 4: write modified serial */
    log_info("[path_b_id] Writing modified serial descriptor...");
    rc = path_b_write_serial(dev, modified);
    if (rc != 0) {
        log_error("[path_b_id] Failed to write modified serial");
        return -1;
    }

    /* Step 5: verify by reading back */
    log_info("[path_b_id] Verifying serial descriptor...");
    rc = path_b_read_serial(dev, verify, sizeof(verify));
    if (rc != 0) {
        log_warn("[path_b_id] Could not verify serial (read-back failed)");
        /* Non-fatal: write may have succeeded even if re-read fails. */
        return 0;
    }

    if (strstr(verify, "PWND:") == NULL) {
        log_error("[path_b_id] Verification failed: PWND marker not found");
        log_error("[path_b_id] Read-back serial: %s", verify);
        return -1;
    }

    log_info("[path_b_id] Identity manipulation verified");
    return 0;
}
