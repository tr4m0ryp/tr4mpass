/*
 * afc_utils.h -- AFC file operations wrapper.
 *
 * Provides convenience functions over the libimobiledevice AFC API for
 * reading, writing, listing, and checking files on the device filesystem.
 * AFC root maps to /var/mobile/Media/ on the device.
 */

#ifndef AFC_UTILS_H
#define AFC_UTILS_H

#include <stdint.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/afc.h>
#include "device/device.h"

/*
 * afc_connect_service -- Start an AFC service by name and return a client.
 * Use "com.apple.afc" for sandboxed access (/var/mobile/Media/) or
 * "com.apple.afc2" for full root filesystem access (jailbroken).
 * Caller must call afc_disconnect() when done.
 * Returns 0 on success, -1 on error.
 */
int afc_connect_service(device_info_t *dev, afc_client_t *client,
                        const char *service_name);

/*
 * afc_connect -- Start the default AFC service ("com.apple.afc").
 * Convenience wrapper around afc_connect_service().
 * Caller must call afc_disconnect() when done.
 * Returns 0 on success, -1 on error.
 */
int afc_connect(device_info_t *dev, afc_client_t *client);

/*
 * afc_disconnect -- Free the AFC client handle.
 */
void afc_disconnect(afc_client_t client);

/*
 * afc_write_file -- Write data to a file on the device.
 * Creates or overwrites the file at the given AFC path.
 * Returns bytes written on success, -1 on error.
 */
int afc_write_file(afc_client_t client, const char *path,
                   const char *data, uint32_t len);

/*
 * afc_read_file -- Read a file from the device into buf.
 * Reads up to max_len bytes. Returns bytes read on success, -1 on error.
 */
int afc_read_file(afc_client_t client, const char *path,
                  char *buf, uint32_t max_len);

/*
 * afc_list_dir -- Print directory listing to stdout.
 * Returns number of entries on success, -1 on error.
 */
int afc_list_dir(afc_client_t client, const char *path);

/*
 * afc_file_exists -- Check if a file or directory exists.
 * Returns 1 if exists, 0 if not, -1 on error.
 */
int afc_file_exists(afc_client_t client, const char *path);

/*
 * afc_remove_file -- Remove a file at the given path.
 * Returns 0 on success, -1 on error.
 */
int afc_remove_file(afc_client_t client, const char *path);

#endif /* AFC_UTILS_H */
