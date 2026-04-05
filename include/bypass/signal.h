/*
 * signal.h -- Cellular vs WiFi-only signal type detection.
 *
 * Determines whether a device is cellular-capable (GSM, CDMA, or dual)
 * or WiFi-only, based on IMEI and MEID fields from device_info_t.
 * Used by Path B to select the appropriate activation record strategy.
 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <stddef.h>
#include "device/device.h"

/*
 * signal_type_t -- Cellular capability classification.
 */
typedef enum {
    SIGNAL_NONE,    /* WiFi-only device (iPad WiFi, iPod)   */
    SIGNAL_GSM,     /* GSM device (IMEI present, no MEID)   */
    SIGNAL_MEID,    /* CDMA device (MEID present, no IMEI)  */
    SIGNAL_DUAL     /* Both IMEI and MEID present            */
} signal_type_t;

/*
 * signal_detect_type -- Classify the device based on IMEI/MEID fields.
 * Returns SIGNAL_NONE if neither field is populated.
 */
signal_type_t signal_detect_type(const device_info_t *dev);

/*
 * signal_can_preserve -- Check if signal preservation is possible.
 * Returns 1 for any cellular type, 0 for SIGNAL_NONE (WiFi-only).
 */
int signal_can_preserve(const device_info_t *dev);

/*
 * signal_get_carrier_id -- Retrieve the carrier identifier for the device.
 * Prefers MEID over IMEI (more reliable for activation records per research).
 * Writes the identifier into buf (up to len bytes including NUL terminator).
 * Returns 0 on success, -1 if neither MEID nor IMEI is available or buf is NULL.
 */
int signal_get_carrier_id(const device_info_t *dev, char *buf, size_t len);

/*
 * signal_print_info -- Print signal detection results to stdout via log_info().
 */
void signal_print_info(const device_info_t *dev);

#endif /* SIGNAL_H */
