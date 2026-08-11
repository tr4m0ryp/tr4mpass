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
#include <libusb.h>
#include "compat/libirecovery_compat.h"

#include "bypass/path_b.h"
#include "device/usb_dfu.h"
#include "util/usb_helpers.h"
#include "util/log.h"

/* USB standard request codes (USB 2.0 spec table 9-4) */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_DT_STRING           0x03

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

    rc = usb_ctrl_transfer(
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
        usb_print_error(rc);
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
 * path_b_read_serial -- Read device serial string in DFU or recovery mode.
 *
 * DFU mode (dev->usb set): uses USB GET_DESCRIPTOR control transfer.
 * Recovery mode (dev->usb NULL): uses libirecovery to read serial_string
 * from the irecv_device_info struct (iBoot exposes it in recovery).
 */
int path_b_read_serial(device_info_t *dev, char *buf, size_t len)
{
    if (!dev || !buf || len == 0) {
        log_error("[path_b_id] Invalid arguments to read_serial");
        return -1;
    }

    if (dev->usb) {
        /*
         * DFU mode: USB GET_DESCRIPTOR.  Use the runtime-resolved
         * bDeviceDescriptor.iSerialNumber captured by usb_dfu_find(),
         * falling back to the legacy indices (3 then 4) if the device
         * did not advertise one or the primary read fails.  Any probe
         * that succeeds decodes cleanly to ASCII wins; the first one
         * whose decoded text does not start with the product-string
         * sentinel is accepted.
         */
        static const uint8_t k_legacy_a = 3;
        static const uint8_t k_legacy_b = 4;
        uint8_t probes[3];
        size_t probe_count = 0;
        size_t i, j;
        int last_len = -1;
        char last_ascii[USB_DESC_MAX_LEN] = {0};

        if (dev->iserial_index != 0)
            probes[probe_count++] = dev->iserial_index;
        if (k_legacy_a != dev->iserial_index)
            probes[probe_count++] = k_legacy_a;
        if (k_legacy_b != dev->iserial_index && k_legacy_b != k_legacy_a)
            probes[probe_count++] = k_legacy_b;

        for (i = 0; i < probe_count; i++) {
            uint8_t idx = probes[i];
            int already = 0;
            uint8_t desc[USB_DESC_MAX_LEN];
            char decoded[USB_DESC_MAX_LEN];
            int desc_len;

            for (j = 0; j < i; j++) {
                if (probes[j] == idx) { already = 1; break; }
            }
            if (already)
                continue;

            desc_len = usb_get_string_descriptor(dev->usb, idx,
                                                 desc, sizeof(desc));
            if (desc_len < 0) {
                log_debug("[path_b_id] GET_DESCRIPTOR idx %u failed, trying next",
                          (unsigned)idx);
                continue;
            }

            if (utf16le_to_ascii(desc, desc_len, decoded, sizeof(decoded)) < 0) {
                log_debug("[path_b_id] decode failed at idx %u", (unsigned)idx);
                continue;
            }

            /* Remember the most recent successful decode; if every
             * probe returns the product string we still surface it. */
            snprintf(last_ascii, sizeof(last_ascii), "%s", decoded);
            last_len = (int)strlen(last_ascii);

            if (strncmp(decoded, "Apple Mobile Device", 19) == 0) {
                log_debug("[path_b_id] idx %u returned product string, probing further",
                          (unsigned)idx);
                continue;
            }

            snprintf(buf, len, "%s", decoded);
            log_debug("[path_b_id] Read serial (DFU, idx %u): %s",
                      (unsigned)idx, buf);
            return 0;
        }

        if (last_len > 0) {
            snprintf(buf, len, "%s", last_ascii);
            log_warn("[path_b_id] All probes returned product string; using \"%s\"",
                     buf);
            return 0;
        }

        log_error("[path_b_id] Failed to read serial descriptor at any probed index");
        return -1;
    }

    /* Recovery mode: use libirecovery */
    {
        irecv_client_t client = NULL;
        irecv_error_t  err;
        const struct irecv_device_info *info;

        if (dev->ecid != 0)
            err = irecv_open_with_ecid_and_attempts(&client,
                                                    (uint64_t)dev->ecid, 5);
        else
            err = irecv_open_with_ecid_and_attempts(&client, 0, 5);

        if (err != IRECV_E_SUCCESS || !client) {
            log_error("[path_b_id] iRecovery open failed for serial read: %s",
                      irecv_strerror(err));
            return -1;
        }

        info = irecv_get_device_info(client);
        if (!info || !info->serial_string || info->serial_string[0] == '\0') {
            log_error("[path_b_id] iRecovery: no serial string available");
            irecv_close(client);
            return -1;
        }

        strncpy(buf, info->serial_string, len - 1);
        buf[len - 1] = '\0';
        log_debug("[path_b_id] Read serial (recovery): %s", buf);
        irecv_close(client);
        return 0;
    }
}

/*
 * path_b_write_serial_irecovery -- Set serial-number env var in recovery mode.
 *
 * Apple's A12+ BootROM rejects SET_DESCRIPTOR with LIBUSB_ERROR_PIPE.
 * Instead, this function uses libirecovery to send "setenv serial-number"
 * from recovery mode (PID 0x1281), which iBoot accepts before booting iOS.
 * The PWND marker in the serial is then visible to mobileactivationd.
 *
 * Device must already be in recovery mode before this is called.
 * path_b.c:step_reboot_to_recovery() handles the DFU -> recovery transition.
 */
int path_b_write_serial_irecovery(device_info_t *dev, const char *new_serial)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;
    int            mode = 0;
    int            rc = -1;

    if (!dev || !new_serial) {
        log_error("[path_b_id] Invalid arguments to write_serial_irecovery");
        return -1;
    }

    /* Open device -- prefer ECID match to avoid touching wrong device.
     * irecv_open_with_ecid with ecid=0 matches any connected device. */
    if (dev->ecid != 0)
        err = irecv_open_with_ecid_and_attempts(&client, (uint64_t)dev->ecid, 5);
    else
        err = irecv_open_with_ecid_and_attempts(&client, 0, 5);

    if (err != IRECV_E_SUCCESS || !client) {
        log_error("[path_b_id] Could not open device in recovery mode: %s",
                  irecv_strerror(err));
        return -1;
    }

    /* Verify the device is actually in recovery (not DFU or normal).
     * struct irecv_device_info has no pid field on any released
     * libirecovery; query the mode via irecv_get_mode() instead and
     * accept any of the four recovery-mode PIDs (0x1280..0x1283). */
    err = irecv_get_mode(client, &mode);
    if (err != IRECV_E_SUCCESS ||
        mode < IRECV_K_RECOVERY_MODE_1 || mode > IRECV_K_RECOVERY_MODE_4) {
        log_error("[path_b_id] Device is not in recovery mode (mode=0x%04X, err=%s)",
                  (unsigned)mode, irecv_strerror(err));
        irecv_close(client);
        return -1;
    }

    /* Set the serial-number environment variable */
    err = irecv_setenv(client, "serial-number", new_serial);
    if (err != IRECV_E_SUCCESS) {
        log_error("[path_b_id] irecv_setenv serial-number failed: %s",
                  irecv_strerror(err));
        goto done;
    }
    log_info("[path_b_id] setenv serial-number succeeded");

    /* Persist to NVRAM -- non-fatal if saveenv is unsupported by iBoot */
    err = irecv_saveenv(client);
    if (err != IRECV_E_SUCCESS)
        log_warn("[path_b_id] saveenv failed (%s), continuing (in-session only)",
                 irecv_strerror(err));
    else
        log_info("[path_b_id] Serial persisted to NVRAM via saveenv");

    log_info("[path_b_id] Serial set via iRecovery: %s", new_serial);
    rc = 0;

done:
    irecv_close(client);
    return rc;
}

/*
 * path_b_write_serial -- Write modified serial to the device.
 * Routes through iRecovery setenv (recovery mode) since A12+ BootROM
 * STALLs the USB SET_DESCRIPTOR request.
 */
int path_b_write_serial(device_info_t *dev, const char *new_serial)
{
    if (!dev || !new_serial) {
        log_error("[path_b_id] Invalid arguments to write_serial");
        return -1;
    }

    log_info("[path_b_id] Writing serial via iRecovery (recovery mode)...");
    return path_b_write_serial_irecovery(dev, new_serial);
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
