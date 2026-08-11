/* usb_dfu.c -- Apple DFU mode device detection and raw USB I/O (libusb) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

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

/* Legacy fallback string indices tried when the device does not
 * advertise a serial descriptor via bDeviceDescriptor.iSerialNumber.
 * Pre-A9 iBoot exposes CPID/ECID at index 3; A14+ moves it to 4. */
#define DFU_SERIAL_LEGACY_A 3
#define DFU_SERIAL_LEGACY_B 4

/* Module-global libusb context */
static libusb_context *g_ctx = NULL;

/*
 * Serial descriptor cached before interface claim.
 * libusb_get_string_descriptor_ascii succeeds on the default control
 * pipe without any interface being claimed.  After libusb_claim_interface
 * is called some USB-IP / usbipd stacks reconfigure their virtual device
 * state and the same read returns 0 bytes.  Reading once before claiming
 * and caching the result works around this.
 */
static char    g_pre_claim_serial[DFU_SERIAL_MAX];
static uint8_t g_pre_claim_iserial_idx;

int usb_dfu_init(void)
{
    int ret;
    if (g_ctx)
        return 0;
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
    }
}

libusb_context *usb_dfu_ctx(void)
{
    return g_ctx;
}

int usb_dfu_find(libusb_device_handle **handle, uint8_t *iserial_out)
{
    libusb_device **devs = NULL;
    ssize_t count;
    ssize_t i;
    int found = 0;

    if (!handle)
        return -1;
    *handle = NULL;
    if (iserial_out)
        *iserial_out = 0;

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
            if (iserial_out)
                *iserial_out = desc.iSerialNumber;
            log_info("DFU device found (bus %d, addr %d, iSerial=%u)",
                     libusb_get_bus_number(devs[i]),
                     libusb_get_device_address(devs[i]),
                     (unsigned)desc.iSerialNumber);

            /*
             * Read the serial string descriptor NOW, before claiming the
             * interface.  On usbipd-win, libusb_claim_interface causes
             * the virtual device to drop into a state where descriptor
             * reads on EP0 return 0 bytes.  Cache the result for
             * usb_dfu_read_info() to use as its primary source.
             *
             * Brief settle delay: usbipd establishes its vhci_hcd TCP
             * channel asynchronously after libusb_open returns.  Without
             * this delay the first control transfer times out even though
             * the device is physically present.
             */
            usleep(500000);  /* 500 ms: let usbipd vhci channel settle */
            g_pre_claim_serial[0] = '\0';
            g_pre_claim_iserial_idx = desc.iSerialNumber;
            if (desc.iSerialNumber != 0) {
                int n = 0;
                int attempt;
                for (attempt = 0; attempt < 5; attempt++) {
                    n = libusb_get_string_descriptor_ascii(
                            *handle, desc.iSerialNumber,
                            (unsigned char *)g_pre_claim_serial,
                            (int)sizeof(g_pre_claim_serial) - 1);
                    if (n > 0)
                        break;
                    if (attempt < 4)
                        usleep(200000);  /* usbipd control pipe may still be initializing */
                }
                if (n > 0) {
                    g_pre_claim_serial[n] = '\0';
                    log_debug("pre-claim serial (idx %u): %s",
                              (unsigned)desc.iSerialNumber, g_pre_claim_serial);
                } else {
                    g_pre_claim_serial[0] = '\0';
                    log_debug("pre-claim serial read failed after retries (idx %u): %s",
                              (unsigned)desc.iSerialNumber,
                              n < 0 ? libusb_strerror(n) : "empty");
                }
            }

            found = 1;
            break;
        }
    }

    libusb_free_device_list(devs, 1);

    if (!found) {
        log_debug("no Apple DFU device found");
        return -1;
    }

    (void)libusb_detach_kernel_driver(*handle, 0);

    /* Claim interface 0 (DFU interface) */
    {
        int ret = libusb_claim_interface(*handle, 0);
        if (ret == LIBUSB_ERROR_BUSY) {
            log_warn("claim interface 0 busy -- forcing usbipd reattach to clear stale state");
            libusb_close(*handle);
            *handle = NULL;

            libusb_free_device_list(devs, 1);
            devs = NULL;

            if (getenv("WSL_DISTRO_NAME")) {
                (void)system("powershell.exe -NoProfile -NonInteractive -Command "
                       "'try { usbipd detach --hardware-id 05ac:1227 2>$null } catch {}; "
                       "Start-Sleep -Milliseconds 2000; "
                       "try { usbipd attach --hardware-id 05ac:1227 --wsl 2>$null } catch {}' "
                       ">/dev/null 2>&1");
                sleep(3);
            }
            return -1;
        }
        if (ret != LIBUSB_SUCCESS)
            log_warn("failed to claim interface 0: %s (continuing anyway)",
                     libusb_strerror(ret));
    }

    return 0;
}

/* Parse hex value after key prefix (e.g. "CPID:" -> 0x8015). */
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

/*
 * Fallback descriptor reader for usbipd/libusb stacks where
 * libusb_get_string_descriptor_ascii() times out while fetching language IDs.
 * Reads the UTF-16LE string directly with a fixed langid and converts to ASCII.
 */
static int read_string_descriptor_utf16_ascii(libusb_device_handle *handle,
                                              uint8_t index,
                                              unsigned char *buf, size_t buf_len)
{
    unsigned char raw[DFU_SERIAL_MAX * 2];
    int ret;
    int raw_len;
    int out = 0;
    int i;

    if (!handle || !buf || buf_len < 2 || index == 0)
        return LIBUSB_ERROR_INVALID_PARAM;

    ret = libusb_get_string_descriptor(handle, index, 0x0409, raw, (int)sizeof(raw));
    if (ret < 0)
        return ret;
    raw_len = ret;
    if (raw_len < 2 || raw[1] != LIBUSB_DT_STRING)
        return LIBUSB_ERROR_IO;

    for (i = 2; i + 1 < raw_len && out < (int)buf_len - 1; i += 2) {
        unsigned char lo = raw[i];
        unsigned char hi = raw[i + 1];
        if (hi == 0 && lo >= 0x20 && lo <= 0x7E) {
            buf[out++] = lo;
        } else if (hi == 0 && (lo == '\r' || lo == '\n' || lo == '\t')) {
            buf[out++] = lo;
        } else {
            buf[out++] = '?';
        }
    }
    buf[out] = '\0';
    return out;
}

/*
 * try_read_serial_descriptor -- Read one string descriptor at `index`
 * into buf (NUL-terminated on success).  Retries transient PIPE/TIMEOUT
 * errors up to twice.  Returns the number of ASCII bytes read (>=0) on
 * success, or the libusb error on hard failure.  A returned length of
 * zero is treated as a hard failure -- the descriptor exists but is
 * empty, which no Apple DFU firmware produces for CPID/ECID.
 */
static int try_read_serial_descriptor(libusb_device_handle *handle,
                                      uint8_t index,
                                      unsigned char *buf, size_t buf_len)
{
    int ret = LIBUSB_ERROR_INVALID_PARAM;
    int attempt;

    if (index == 0)
        return LIBUSB_ERROR_INVALID_PARAM;

    for (attempt = 0; attempt < 3; attempt++) {
        ret = libusb_get_string_descriptor_ascii(handle, index,
                                                 buf, (int)buf_len);
        if (ret > 0)
            break;
        if (ret == 0 || ret == LIBUSB_ERROR_TIMEOUT) {
            int fb = read_string_descriptor_utf16_ascii(handle, index, buf, buf_len);
            if (fb > 0)
                return fb;
        }
        if (ret >= 0)
            break;
        if (ret != LIBUSB_ERROR_PIPE && ret != LIBUSB_ERROR_TIMEOUT)
            break;
        if (attempt < 2) {
            log_warn("serial descriptor read at idx %u: %s (attempt %d/3, retrying)",
                     (unsigned)index, libusb_strerror(ret), attempt + 1);
            usleep(50000);
        }
    }
    return ret;
}

int usb_dfu_read_info(libusb_device_handle *handle, uint8_t iserial_hint,
                      uint32_t *cpid, uint64_t *ecid,
                      char *serial, size_t serial_len)
{
    unsigned char buf[DFU_SERIAL_MAX];
    unsigned char sentinel_buf[DFU_SERIAL_MAX];
    int sentinel_len = 0;
    int all_product_string = 1;
    int any_success = 0;
    int chosen_len = 0;
    uint64_t val;
    uint8_t probe_order[3];
    size_t probe_count = 0;
    size_t i, j;

    if (!handle)
        return -1;

    /* Initialize outputs to safe defaults */
    if (cpid) *cpid = 0;
    if (ecid) *ecid = 0;
    if (serial && serial_len > 0) serial[0] = '\0';
    buf[0] = '\0';
    sentinel_buf[0] = '\0';

    /*
     * Build a deduplicated probe order.  Hint first (typically the
     * device's own bDeviceDescriptor.iSerialNumber), then the two
     * legacy indices that cover every shipped Apple iBoot layout.
     *
     * Special case: if a pre-claim serial was cached in usb_dfu_find,
     * use it directly when the hint index matches, avoiding a descriptor
     * read on the now-claimed interface (which may return 0 on usbipd).
     */
    if (iserial_hint != 0 &&
        g_pre_claim_serial[0] != '\0' &&
        g_pre_claim_iserial_idx == iserial_hint &&
        strstr(g_pre_claim_serial, "CPID:") != NULL) {

        log_debug("usb_dfu_read_info: using pre-claim serial cache (idx %u): %s",
                  (unsigned)iserial_hint, g_pre_claim_serial);
        chosen_len = (int)strlen(g_pre_claim_serial);
        memcpy(buf, g_pre_claim_serial, (size_t)chosen_len + 1);
        any_success = 1;
        all_product_string = 0;
    }

    if (!any_success) {
        if (iserial_hint != 0)
            probe_order[probe_count++] = iserial_hint;
        if (DFU_SERIAL_LEGACY_A != iserial_hint)
            probe_order[probe_count++] = DFU_SERIAL_LEGACY_A;
        if (DFU_SERIAL_LEGACY_B != iserial_hint &&
            DFU_SERIAL_LEGACY_B != DFU_SERIAL_LEGACY_A)
            probe_order[probe_count++] = DFU_SERIAL_LEGACY_B;

        for (i = 0; i < probe_count; i++) {
            uint8_t idx = probe_order[i];
            int ret;

            /* Skip if we already tried this index (defensive; the dedupe
             * above should have caught it). */
            int already = 0;
            for (j = 0; j < i; j++) {
                if (probe_order[j] == idx) { already = 1; break; }
            }
            if (already)
                continue;

            ret = try_read_serial_descriptor(handle, idx, buf, sizeof(buf));
            if (ret <= 0) {
                log_debug("serial descriptor idx %u: %s",
                          (unsigned)idx,
                          ret == 0 ? "empty" : libusb_strerror(ret));
                continue;
            }

            if (ret >= (int)sizeof(buf))
                ret = (int)sizeof(buf) - 1;
            buf[ret] = '\0';
            any_success = 1;
            log_debug("DFU serial string (idx %u): %s", (unsigned)idx, (char *)buf);

            /*
             * When the read returns the human-readable product string, the
             * device exposed CPID/ECID at a different descriptor.  Record
             * the last one we saw so the sentinel diagnostic can still
             * report what the user is seeing, then keep probing.
             */
            if (strncmp((char *)buf, "Apple Mobile Device", 19) == 0) {
                memcpy(sentinel_buf, buf, (size_t)ret + 1);
                sentinel_len = ret;
                continue;
            }

            /* Non-product string: this is the descriptor we want. */
            all_product_string = 0;
            chosen_len = ret;
            log_info("DFU serial descriptor resolved at index %u", (unsigned)idx);
            break;
        }
    } /* end if (!any_success) probe loop */

    if (!any_success) {
        log_error("failed to read serial descriptor at any probed index");
        return -1;
    }

    if (all_product_string) {
        /*
         * Every probe returned the product string, so the caller must
         * either provide --cpid/--ecid or re-enter true SecureROM DFU.
         * Report through the same success path so caller-level override
         * logic (main.c) can still fire.
         */
        if (sentinel_len > 0) {
            if (sentinel_len >= (int)sizeof(sentinel_buf))
                sentinel_len = (int)sizeof(sentinel_buf) - 1;
            sentinel_buf[sentinel_len] = '\0';
            if (serial && serial_len > 0) {
                strncpy(serial, (char *)sentinel_buf, serial_len - 1);
                serial[serial_len - 1] = '\0';
            }
        }
        log_error("DFU serial descriptor at every probed index returned only the product string.");
        log_info("This usually means the device exposes CPID/ECID at a different index");
        log_info("than the tool probes (or is not in true SecureROM DFU).  Verify with");
        log_info("`irecovery -q` and re-run with `--cpid 0xXXXX --ecid 0xYYYYYY`,");
        log_info("or re-enter DFU with the correct button sequence:");
        log_info("  Home button:  Power+Home 10s, release Power, hold Home 5s");
        log_info("  Face ID:      Vol-Up, Vol-Down, hold Side to black screen,");
        log_info("                Side+Vol-Down 5s, release Side, hold Vol-Down 10s");
        log_info("Screen must stay completely BLACK (no Apple logo).");
        return 0;
    }

    /* Copy full serial string to caller */
    if (serial && serial_len > 0) {
        strncpy(serial, (char *)buf, serial_len - 1);
        serial[serial_len - 1] = '\0';
    }
    (void)chosen_len;

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

void usb_dfu_close(libusb_device_handle *handle)
{
    if (!handle)
        return;

    libusb_release_interface(handle, 0);
    libusb_close(handle);
    /* Invalidate the pre-claim serial cache so the next usb_dfu_find
     * reads a fresh descriptor (important after exploit -- the serial
     * may now contain "PWND:" which the stale cache would hide). */
    g_pre_claim_serial[0] = '\0';
    g_pre_claim_iserial_idx = 0;
    log_debug("DFU device handle closed");
}
