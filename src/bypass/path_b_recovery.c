/*
 * path_b_recovery.c -- DFU to recovery transition with detailed USB logging.
 */

#include "bypass/path_b_recovery.h"
#include "device/device.h"
#include "device/usb_dfu.h"
#include "exploit/dfu_proto.h"
#include "util/log.h"

#include <libirecovery.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define REBOOT_POLL_USEC          2000000
#define RECOVERY_WAIT_SECS        120
#define AUTO_RECOVERY_WAIT_SECS   60
#define POST_CLOSE_USEC           200000

#define APPLE_VID           0x05AC
#define DFU_PID             0x1227
#define RECOVERY_PID_MIN    0x1280
#define RECOVERY_PID_MAX    0x1283

static const char *usb_pid_label(uint16_t pid)
{
    if (pid == DFU_PID)
        return "DFU";
    if (pid >= RECOVERY_PID_MIN && pid <= RECOVERY_PID_MAX)
        return "Recovery";
    return "Apple";
}

static int is_recovery_pid(uint16_t pid)
{
    return pid >= RECOVERY_PID_MIN && pid <= RECOVERY_PID_MAX;
}

static void log_apple_usb_snapshot(libusb_context *ctx)
{
    libusb_device **devs = NULL;
    ssize_t         count;
    ssize_t         i;
    int             apple_count = 0;

    if (!ctx)
        return;

    count = libusb_get_device_list(ctx, &devs);
    if (count < 0) {
        log_info("[path_b] USB snapshot: libusb_get_device_list failed: %s",
                 libusb_strerror((int)count));
        return;
    }

    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(devs[i], &desc) != LIBUSB_SUCCESS)
            continue;
        if (desc.idVendor != APPLE_VID)
            continue;

        log_info("[path_b] Apple USB: pid=0x%04X (%s) bus=%u addr=%u",
                 desc.idProduct, usb_pid_label(desc.idProduct),
                 libusb_get_bus_number(devs[i]),
                 libusb_get_device_address(devs[i]));
        apple_count++;
    }

    if (apple_count == 0)
        log_info("[path_b] Apple USB: (none visible)");

    libusb_free_device_list(devs, 1);
}

static void log_dfu_status(libusb_device_handle *usb)
{
    dfu_status_t st;

    if (!usb)
        return;

    if (dfu_get_status(usb, &st) != 0) {
        log_info("[path_b] DFU GETSTATUS failed before transition");
        return;
    }

    log_info("[path_b] DFU state before transition: state=0x%02X status=0x%02X",
             st.bState, st.bStatus);
}

static void print_manual_recovery_instructions(void)
{
    log_info("[path_b] Automatic recovery entry did not succeed.");
    log_info("[path_b] Enter recovery manually now:");
    log_info("[path_b]   Face ID iPad/iPhone: Vol-Up, Vol-Down, hold Side to black,");
    log_info("[path_b]     then Side+Vol-Down 5s, release Side, hold Vol-Down 10s");
    log_info("[path_b]   Home button: Power+Home 10s, release Power, hold Home 5s");
    log_info("[path_b]   Screen should show cable-to-computer icon (not black DFU).");
    log_info("[path_b]   Or set TR4MPASS_IPSW_PATH / TR4MPASS_IBSS_PATH and re-run.");
}

static void backfill_device_ids(device_info_t *dev,
                                const struct irecv_device_info *info)
{
    if (!dev || !info)
        return;
    if (dev->ecid == 0 && info->ecid != 0)
        dev->ecid = info->ecid;
    if (dev->cpid == 0 && info->cpid != 0)
        dev->cpid = info->cpid;
}

static int try_irecv_open(device_info_t *dev, uint64_t ecid, uint16_t *pid_out)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;
    const struct irecv_device_info *info;

    err = irecv_open_with_ecid_and_attempts(&client, ecid, 1);
    if (err != IRECV_E_SUCCESS || !client) {
        log_info("[path_b] irecv_open: %s", irecv_strerror(err));
        return 0;
    }

    info = irecv_get_device_info(client);
    if (info) {
        backfill_device_ids(dev, info);
        log_info("[path_b] irecv_open: success pid=0x%04X serial=%s",
                 info->pid,
                 info->serial_string ? info->serial_string : "(none)");
        if (pid_out)
            *pid_out = info->pid;
        if (is_recovery_pid(info->pid)) {
            irecv_close(client);
            return 1;
        }
    } else {
        log_info("[path_b] irecv_open: connected but no device info");
    }

    irecv_close(client);
    return 0;
}

static int usb_list_has_recovery(libusb_context *ctx)
{
    libusb_device **devs = NULL;
    ssize_t         count;
    ssize_t         i;
    int             found = 0;

    count = libusb_get_device_list(ctx, &devs);
    if (count < 0)
        return 0;

    for (i = 0; i < count && !found; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) != LIBUSB_SUCCESS)
            continue;
        if (desc.idVendor == APPLE_VID && is_recovery_pid(desc.idProduct))
            found = 1;
    }

    libusb_free_device_list(devs, 1);
    return found;
}

static int upload_ibss(uint64_t ecid, const char *ibss_path)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;

    log_info("[path_b] Opening DFU device via libirecovery (ecid=0x%llX)...",
             (unsigned long long)ecid);

    err = irecv_open_with_ecid_and_attempts(&client, ecid, 5);
    if (err != IRECV_E_SUCCESS || !client) {
        log_error("[path_b] irecv_open for iBSS upload failed: %s",
                  irecv_strerror(err));
        return -1;
    }

    log_info("[path_b] Uploading iBSS: %s", ibss_path);
    err = irecv_send_file(client, ibss_path, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
    irecv_close(client);

    if (err != IRECV_E_SUCCESS) {
        log_error("[path_b] irecv_send_file(iBSS) failed: %s",
                  irecv_strerror(err));
        return -1;
    }

    log_info("[path_b] iBSS upload finished, waiting for recovery re-enumeration...");
    usleep(POST_CLOSE_USEC);
    return 0;
}

static int attempt_irecv_reset(uint64_t ecid)
{
    irecv_client_t client = NULL;
    irecv_error_t  err;

    err = irecv_open_with_ecid_and_attempts(&client, ecid, 5);
    if (err != IRECV_E_SUCCESS || !client) {
        log_warn("[path_b] irecv_open for USB reset failed: %s",
                 irecv_strerror(err));
        return -1;
    }

    err = irecv_reset(client);
    irecv_close(client);
    if (err != IRECV_E_SUCCESS) {
        log_warn("[path_b] irecv_reset failed: %s", irecv_strerror(err));
        return -1;
    }

    log_info("[path_b] USB reset sent via irecv, waiting for re-enumeration...");
    usleep(POST_CLOSE_USEC);
    return 0;
}

static int attempt_auto_recovery_boot(uint64_t ecid, const char *ibss_path)
{
    if (ibss_path && ibss_path[0]) {
        log_info("[path_b] Automatic recovery: uploading signed iBSS...");
        return upload_ibss(ecid, ibss_path);
    }

    log_info("[path_b] Automatic recovery: no iBSS available, trying USB reset...");
    return attempt_irecv_reset(ecid);
}

static void log_timeout_diagnosis(libusb_context *ctx, int saw_dfu, int saw_none)
{
    log_error("[path_b] Timed out waiting for recovery mode (%ds)",
              RECOVERY_WAIT_SECS);
    log_apple_usb_snapshot(ctx);

    if (saw_dfu)
        log_error("[path_b] Diagnosis: device still in DFU (0x1227). "
                  "Set TR4MPASS_IPSW_PATH or enter recovery manually.");
    else if (saw_none)
        log_error("[path_b] Diagnosis: no Apple USB device visible. "
                  "Check cable, port, and WSL usbipd attachment.");
    else
        log_error("[path_b] Diagnosis: Apple device present but not in recovery. "
                  "Try TR4MPASS_IPSW_PATH or manual recovery entry.");
}

static int poll_recovery_once(device_info_t *dev, uint64_t ecid,
                              libusb_context *ctx, int *saw_dfu, int *saw_none)
{
    uint16_t pid = 0;

    if (try_irecv_open(dev, ecid, &pid))
        return 1;

    log_apple_usb_snapshot(ctx);

    if (usb_list_has_recovery(ctx)) {
        log_info("[path_b] Recovery PID visible on USB bus");
        return 1;
    }

    {
        libusb_device **devs = NULL;
        ssize_t count = libusb_get_device_list(ctx, &devs);
        int apple = 0;

        if (count > 0) {
            ssize_t i;
            for (i = 0; i < count; i++) {
                struct libusb_device_descriptor desc;
                if (libusb_get_device_descriptor(devs[i], &desc) != 0)
                    continue;
                if (desc.idVendor != APPLE_VID)
                    continue;
                apple++;
                if (desc.idProduct == DFU_PID)
                    *saw_dfu = 1;
            }
        }
        if (devs)
            libusb_free_device_list(devs, 1);
        if (apple == 0)
            *saw_none = 1;
    }

    return 0;
}

int path_b_reboot_to_recovery(device_info_t *dev, const char *ibss_path)
{
    libusb_context *ctx = NULL;
    uint64_t        ecid;
    int             elapsed = 0;
    int             manual_prompted = 0;
    int             saw_dfu = 0;
    int             saw_none = 0;
    int             auto_rc;

    if (!dev) {
        log_error("[path_b] NULL device in reboot_to_recovery");
        return -1;
    }

    if (!dev->usb) {
        log_error("[path_b] No USB handle -- device not in DFU mode");
        return -1;
    }

    log_info("[path_b] Step 1/10: Rebooting device from DFU to recovery mode...");

    ecid = dev->ecid;
    log_dfu_status(dev->usb);

    usb_dfu_close(dev->usb);
    dev->usb         = NULL;
    dev->is_dfu_mode = 0;

    log_info("[path_b] DFU handle released, attempting automatic recovery entry...");

    auto_rc = attempt_auto_recovery_boot(ecid, ibss_path);
    if (auto_rc != 0)
        log_warn("[path_b] Automatic recovery boot step failed (will retry via poll)");

    if (libusb_init(&ctx) != LIBUSB_SUCCESS) {
        log_error("[path_b] libusb_init failed for recovery poll");
        return -1;
    }

    while (elapsed < RECOVERY_WAIT_SECS) {
        const char *phase;

        if (poll_recovery_once(dev, ecid, ctx, &saw_dfu, &saw_none)) {
            phase = manual_prompted ? "manual recovery" : "automatic";
            log_info("[path_b] Recovery mode detected (%ds, via %s)",
                     elapsed, phase);
            libusb_exit(ctx);
            return 0;
        }

        if (!manual_prompted && elapsed >= AUTO_RECOVERY_WAIT_SECS) {
            print_manual_recovery_instructions();
            manual_prompted = 1;
        }

        phase = manual_prompted ? "manual recovery" : "automatic";
        log_info("[path_b] Waiting for recovery... %ds / %ds (%s)",
                 elapsed, RECOVERY_WAIT_SECS, phase);

        usleep(REBOOT_POLL_USEC);
        elapsed += 2;
    }

    log_timeout_diagnosis(ctx, saw_dfu, saw_none);
    libusb_exit(ctx);
    return -1;
}
