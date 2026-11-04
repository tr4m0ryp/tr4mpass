/*
 * mock_hw_stubs.c -- Stubs for hardware-specific routines that the test
 * binary does not include (everything under src/dfu/ and
 * src/device/usb_dfu.c).
 *
 * The mock test build excludes those real implementations because they
 * call directly into libusb with chip-specific offsets.  Tests that
 * drive Path A / Path B orchestration need *some* symbol to link
 * against, so by default the stubs succeed quickly.
 */

#include "mock_internal.h"

#include "device/device.h"
#include "device/usb_dfu.h"
#include "exploit/checkm8.h"
#include "exploit/dfu_proto.h"

#if defined(__APPLE__)
#include "exploit/iokit_usb.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Phase 3: let tests force checkm8 stubs to return specific values
 * without faulting the rest of the pipeline. */
static int g_checkm8_exploit_result        = 0;   /* 0 = success */
static int g_checkm8_exploit_result_set    = 0;
static int g_checkm8_verify_pwned_result   = 1;   /* 1 = modified */
static int g_checkm8_verify_pwned_set      = 0;

void mock_checkm8_set_exploit_result(int result)
{
    g_checkm8_exploit_result     = result;
    g_checkm8_exploit_result_set = 1;
}

void mock_checkm8_set_verify_pwned_result(int result)
{
    g_checkm8_verify_pwned_result = result;
    g_checkm8_verify_pwned_set    = 1;
}

void mock_checkm8_reset(void)
{
    g_checkm8_exploit_result      = 0;
    g_checkm8_exploit_result_set  = 0;
    g_checkm8_verify_pwned_result = 1;
    g_checkm8_verify_pwned_set    = 0;
}

/* ------------------------------------------------------------------ */
/* src/dfu/checkm8.c                                              */
/* ------------------------------------------------------------------ */

int checkm8_exploit(device_info_t *dev)
{
    mock_log_append("checkm8_exploit");
    if (!dev)
        return -1;
    if (g_checkm8_exploit_result_set)
        return g_checkm8_exploit_result;
    return 0;
}

int checkm8_verify_pwned(device_info_t *dev)
{
    mock_log_append("checkm8_verify_pwned");
    if (!dev)
        return -1;
    if (g_checkm8_verify_pwned_set)
        return g_checkm8_verify_pwned_result;
    return 1;
}

int checkm8_stage_reset(exploit_ctx_t *ctx)  { (void)ctx; return 0; }
int checkm8_stage_setup(exploit_ctx_t *ctx)  { (void)ctx; return 0; }
int checkm8_stage_spray(exploit_ctx_t *ctx)  { (void)ctx; return 0; }
int checkm8_stage_patch(exploit_ctx_t *ctx)  { (void)ctx; return 0; }

/* ------------------------------------------------------------------ */
/* src/device/usb_dfu.c                                               */
/* ------------------------------------------------------------------ */

int usb_exploit_init(void)    { return 0; }
void usb_exploit_cleanup(void) {}

int usb_dfu_find(libusb_device_handle **handle, uint8_t *iserial_out)
{
    if (handle) *handle = NULL;
    if (iserial_out) *iserial_out = 3;
    mock_log_append("usb_dfu_find");
    return 0;
}

int usb_dfu_read_info(libusb_device_handle *handle, uint8_t iserial_hint,
                      uint32_t *cpid, uint64_t *ecid,
                      char *serial, size_t serial_len)
{
    (void)handle;
    (void)iserial_hint;
    if (cpid)   *cpid = 0x8015;
    if (ecid)   *ecid = 0x001A2B3C4D5E6F70ULL;
    if (serial && serial_len > 0)
        snprintf(serial, serial_len,
                 "CPID:8015 CPRV:11 BDID:0C ECID:001A2B3C4D5E6F70");
    return 0;
}

int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len)
{
    (void)handle; (void)data; (void)len;
    return 0;
}

int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len,
                 size_t *actual)
{
    (void)handle; (void)buf; (void)len;
    if (actual) *actual = 0;
    return 0;
}

void usb_dfu_close(libusb_device_handle *handle)
{
    (void)handle;
    mock_log_append("usb_dfu_close");
}

/* ------------------------------------------------------------------ */
/* src/dfu/dfu_proto.c                                            */
/* ------------------------------------------------------------------ */

int dfu_get_status(libusb_device_handle *dev, dfu_status_t *status)
{
    (void)dev;
    if (status) memset(status, 0, sizeof(*status));
    return 0;
}

int dfu_clr_status(libusb_device_handle *dev)
{ (void)dev; return 0; }

int dfu_get_state(libusb_device_handle *dev, uint8_t *state)
{ (void)dev; if (state) *state = 0x02; return 0; }

int dfu_dnload(libusb_device_handle *dev, uint16_t block_num,
               const void *data, size_t len)
{ (void)dev; (void)block_num; (void)data; (void)len; return 0; }

int dfu_upload(libusb_device_handle *dev, uint16_t block_num,
               void *buf, size_t len, size_t *actual)
{
    (void)dev; (void)block_num; (void)buf; (void)len;
    if (actual) *actual = 0;
    return 0;
}

int dfu_abort(libusb_device_handle *dev)
{ (void)dev; return 0; }

int dfu_send_data(libusb_device_handle *dev, const void *data, size_t len)
{ (void)dev; (void)data; (void)len; return 0; }

int dfu_reset_to_idle(libusb_device_handle *dev)
{ (void)dev; return 0; }

/* ------------------------------------------------------------------ */
/* src/dfu/iokit_usb.c (macOS only)                                   */
/*                                                                    */
/* src/dfu/* is excluded from the mock build, but src/util/           */
/* usb_helpers.c references these symbols on Apple platforms.         */
/* Keep them inactive so the mock build links and uses libusb mocks.  */
/* ------------------------------------------------------------------ */

#if defined(__APPLE__)
int iokit_usb_is_active(void)
{
    return 0;
}

int iokit_usb_ctrl_transfer(uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex,
                            void *data, uint16_t wLength,
                            uint32_t timeout_ms)
{
    (void)bmRequestType; (void)bRequest; (void)wValue; (void)wIndex;
    (void)data; (void)wLength; (void)timeout_ms;
    return -1;
}
#endif
