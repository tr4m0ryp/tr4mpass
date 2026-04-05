/*
 * record.h -- Activation record plist builder.
 *
 * Constructs activation record plists from device information. These
 * records contain AccountToken, DeviceCertificate, FairPlayKeyData,
 * UniqueDeviceCertificate, and related fields needed by the activation
 * protocol.
 */

#ifndef RECORD_H
#define RECORD_H

#include <stdint.h>
#include <plist/plist.h>
#include "device/device.h"

/*
 * record_build -- Build a complete activation record from device info.
 * Returns a new plist dict containing all required activation fields.
 * Caller must free with record_free() or plist_free().
 * Returns NULL on error.
 */
plist_t record_build(const device_info_t *dev);

/*
 * record_build_a12 -- Build an A12+ specific activation record.
 * Uses the FActivation/drmHandshake variant with additional fields
 * required for A12 and newer chipsets (SEP-aware activation).
 * Returns a new plist dict, or NULL on error. Caller must free.
 */
plist_t record_build_a12(const device_info_t *dev);

/*
 * record_to_xml -- Serialize an activation record to XML string.
 * On success, *len is set to the byte length (excluding NUL).
 * Returns a plist-allocated string; caller must free with plist_mem_free().
 */
char *record_to_xml(plist_t record, uint32_t *len);

/*
 * record_free -- Free an activation record plist.
 */
void record_free(plist_t record);

#endif /* RECORD_H */
