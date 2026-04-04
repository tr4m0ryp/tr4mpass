#ifndef USB_DFU_H
#define USB_DFU_H

#include <stddef.h>
#include <stdint.h>
#include <libusb-1.0/libusb.h>

/* Apple USB identifiers */
#define APPLE_VID       0x05AC
#define DFU_PID         0x1227
#define RECOVERY_PID    0x1281

/* DFU class-specific requests */
#define DFU_DNLOAD      1
#define DFU_UPLOAD      2
#define DFU_GETSTATUS   3
#define DFU_CLRSTATUS   4
#define DFU_ABORT       6

/* Default USB timeout in milliseconds */
#define DFU_USB_TIMEOUT 5000

/* Maximum serial string length from DFU descriptor */
#define DFU_SERIAL_MAX  256

/*
 * Initialize the libusb context. Call once at startup.
 * Returns 0 on success, -1 on failure.
 */
int usb_dfu_init(void);

/*
 * Clean up the libusb context. Call once at shutdown.
 */
void usb_dfu_cleanup(void);

/*
 * Find an Apple device in DFU mode (VID=0x05AC, PID=0x1227).
 * On success, *handle is set to an opened device handle and 0 is returned.
 * On failure, *handle is set to NULL and -1 is returned.
 */
int usb_dfu_find(libusb_device_handle **handle);

/*
 * Read device info from the DFU serial string descriptor.
 * Parses CPID and ECID as hex values from the key:value pairs.
 * Copies the full serial string to serial (up to serial_len bytes).
 * Missing fields are set to 0 / empty string.
 * Returns 0 on success, -1 on failure.
 */
int usb_dfu_read_info(libusb_device_handle *handle, uint32_t *cpid,
                      uint64_t *ecid, char *serial, size_t serial_len);

/*
 * Send data to the DFU device via control transfer (DFU_DNLOAD).
 * Returns 0 on success, -1 on failure.
 */
int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len);

/*
 * Receive data from the DFU device via control transfer (DFU_UPLOAD).
 * On success, *actual is set to the number of bytes received and 0 is returned.
 * On failure, *actual is set to 0 and -1 is returned.
 */
int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len,
                 size_t *actual);

/*
 * Close and release the DFU device handle.
 */
void usb_dfu_close(libusb_device_handle *handle);

#endif /* USB_DFU_H */
