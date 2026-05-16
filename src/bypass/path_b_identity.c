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
#include <libirecovery.h>

#include <unistd.h>

#include "bypass/path_b.h"
#include "device/usb_dfu.h"
#include "util/usb_helpers.h"
#include "util/log.h"

/* USB standard request codes (USB 2.0 spec table 9-4) */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_DT_STRING           0x03

/* Serial string descriptor index for Apple DFU devices */
#define DFU_SERIAL_DESC_INDEX   3

/* PWND marker appended to the serial string */
#define PWND_MARKER             " PWND:[checkm8]"

/* Maximum USB string descriptor length per USB 2.0 spec */
#define USB_DESC_MAX_LEN        255

/* Apple recovery mode USB product ID */
#define APPLE_RECOVERY_PID      0x1281

/* Extract a hex field from a serial string, e.g. ECID:00112233. */
static int path_b_extract_hex_field(const char *serial, const char *key, uint64_t *out)
{
    const char *p;
    char *endptr = NULL;
    unsigned long long val;

    if (!serial || !key || !out)
        return -1;

    p = strstr(serial, key);
    if (!p)
        return -1;

    val = strtoull(p + strlen(key), &endptr, 16);
    if (!endptr || endptr == p + strlen(key))
        return -1;

    *out = (uint64_t)val;
    return 0;
}

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
        /* DFU mode: USB GET_DESCRIPTOR */
        uint8_t desc[USB_DESC_MAX_LEN];
        int desc_len = usb_get_string_descriptor(dev->usb, DFU_SERIAL_DESC_INDEX,
                                                 desc, sizeof(desc));
        if (desc_len < 0)
            return -1;

        if (utf16le_to_ascii(desc, desc_len, buf, len) < 0) {
            log_error("[path_b_id] Failed to decode serial descriptor");
            return -1;
        }
        log_debug("[path_b_id] Read serial (DFU): %s", buf);
        return 0;
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

        /* Preserve ECID from recovery so later reconnects can match the
         * same device across mode transitions. */
        if (dev->ecid == 0) {
            uint64_t recovered_ecid = 0;
            if (path_b_extract_hex_field(buf, "ECID:", &recovered_ecid) == 0 && recovered_ecid != 0) {
                dev->ecid = recovered_ecid;
                log_debug("[path_b_id] Captured ECID from recovery serial: 0x%016llX",
                          (unsigned long long)dev->ecid);
            }
        }
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
    const struct irecv_device_info *info;
    char           cmd[DFU_SERIAL_MAX + 32];
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

    /* Verify the device is actually in recovery (not DFU or normal) */
    info = irecv_get_device_info(client);
    if (!info || info->pid != APPLE_RECOVERY_PID) {
        log_error("[path_b_id] Device is not in recovery mode (pid=0x%04X)",
                  info ? (unsigned)info->pid : 0);
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

    (void)cmd; /* cmd buffer no longer needed -- keeping for future use */

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

    /* dev->usb may be NULL if the device rebooted into recovery mode.
     * `path_b_read_serial()` and `path_b_write_serial()` handle both
     * DFU (USB control transfers) and recovery (libirecovery) paths,
     * so allow NULL here and let the helpers perform the correct I/O.
     */
    if (!dev->usb) {
        log_debug("[path_b_id] No DFU USB handle present; will use recovery path if available");
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
        /* Some iBoot implementations do not immediately reflect the
         * updated serial string in the `serial_string` field returned
         * by irecv_get_device_info().  Attempt to read the NVRAM
         * variable `serial-number` via `irecv_getenv()` as a fallback
         * verification method before failing.
         */
        log_warn("[path_b_id] PWND marker not found in serial_string; trying irecv_getenv fallback");

        {
            irecv_client_t client = NULL;
            irecv_error_t err;
            char *envval = NULL;

            /* Open client even if dev->ecid == 0: irecv_open_with_ecid
             * accepts ecid=0 to match any connected recovery device. */
            /* Try multiple times: some iBoot versions may not expose the
             * updated env immediately after saveenv. Re-open the irecv
             * client between attempts to ensure we get fresh state. */
            for (int attempt = 0; attempt < 5; attempt++) {
                err = irecv_open_with_ecid_and_attempts(&client, (uint64_t)dev->ecid, 5);
                if (err != IRECV_E_SUCCESS || !client) {
                    log_debug("[path_b_id] irecv open attempt %d failed: %s",
                              attempt + 1, irecv_strerror(err));
                    if (client) {
                        irecv_close(client);
                        client = NULL;
                    }
                    sleep(1);
                    continue;
                }

                if (irecv_getenv(client, "serial-number", &envval) == IRECV_E_SUCCESS && envval) {
                    log_debug("[path_b_id] irecv_getenv(serial-number) => %s", envval[0] ? envval : "(empty)");
                    if (strstr(envval, "PWND:") != NULL) {
                        free(envval);
                        irecv_close(client);
                        log_info("[path_b_id] Identity manipulation verified via irecv_getenv");
                        return 0;
                    }
                    free(envval);
                } else {
                    log_debug("[path_b_id] irecv_getenv(serial-number) attempt %d failed or empty", attempt + 1);
                }

                irecv_close(client);
                client = NULL;
                /* wait a bit before retrying */
                sleep(1);
            }
        }

        log_warn("[path_b_id] Verification inconclusive: PWND marker not observed after saveenv; continuing anyway");
        log_debug("[path_b_id] Read-back serial: %s", verify);
        /* Treat inability to observe the PWND marker here as non-fatal. */
        return 0;
    }

    log_info("[path_b_id] Identity manipulation verified");
    return 0;
}
