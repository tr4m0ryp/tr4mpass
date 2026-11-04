/* ramdisk.h -- Ramdisk loading helpers for processed DFU mode */

#ifndef RAMDISK_H
#define RAMDISK_H

#include <stddef.h>
#include <stdint.h>

#include "device/device.h"

/* Default path for the ramdisk archive (relative to working directory) */
#define RAMDISK_PATH_LZMA  "payloads/boot.tar.lzma"
#define RAMDISK_PATH_RAW   "payloads/boot.raw"

/* Maximum ramdisk size: 64 MiB */
#define RAMDISK_MAX_SIZE   (64 * 1024 * 1024)

/* DFU transfer chunk size for ramdisk upload */
#define RAMDISK_CHUNK_SIZE 2048

/*
 * ramdisk_load_file -- Read a ramdisk image from disk into a malloc'd buffer.
 * On success, *buf is set to the allocated buffer and *len to its size.
 * Caller must free(*buf) when done.
 * Returns 0 on success, -1 on failure.
 */
int ramdisk_load_file(const char *path, uint8_t **buf, size_t *len);

/*
 * ramdisk_send -- Send a ramdisk image to a device in processed DFU mode.
 * The device must have been stageed (serial contains PWND:) before
 * calling this function.
 * Sends data in RAMDISK_CHUNK_SIZE blocks via the DFU upload interface.
 * Returns 0 on success, -1 on failure.
 */
int ramdisk_send(device_info_t *dev, const uint8_t *buf, size_t len);

/*
 * ramdisk_load_and_send -- Convenience wrapper: loads from path, sends to
 * device, and frees the buffer.
 * Returns 0 on success, -1 on failure.
 */
int ramdisk_load_and_send(device_info_t *dev, const char *path);

#endif /* RAMDISK_H */
