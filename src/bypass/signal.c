/*
 * signal.c -- Cellular vs WiFi-only signal type detection.
 *
 * Classifies device signal capability from IMEI and MEID fields populated
 * by device_query_info(). No network operations are performed here.
 */

#include <string.h>
#include <stdio.h>
#include "bypass/signal.h"
#include "util/log.h"

/* Returns 1 if the field is non-empty, 0 otherwise. */
static int field_present(const char *field)
{
    return (field != NULL && field[0] != '\0');
}

signal_type_t signal_detect_type(const device_info_t *dev)
{
    int has_imei;
    int has_meid;

    if (!dev) {
        log_warn("[signal] NULL device passed to signal_detect_type");
        return SIGNAL_NONE;
    }

    has_imei = field_present(dev->imei);
    has_meid = field_present(dev->meid);

    if (has_meid && has_imei)
        return SIGNAL_DUAL;
    if (has_meid)
        return SIGNAL_MEID;
    if (has_imei)
        return SIGNAL_GSM;

    return SIGNAL_NONE;
}

int signal_can_preserve(const device_info_t *dev)
{
    return (signal_detect_type(dev) != SIGNAL_NONE) ? 1 : 0;
}

int signal_get_carrier_id(const device_info_t *dev, char *buf, size_t len)
{
    if (!dev || !buf || len == 0) {
        log_error("[signal] Invalid arguments to signal_get_carrier_id");
        return -1;
    }

    /* Prefer MEID: more reliable for CDMA activation records per research. */
    if (field_present(dev->meid)) {
        snprintf(buf, len, "%s", dev->meid);
        return 0;
    }

    if (field_present(dev->imei)) {
        snprintf(buf, len, "%s", dev->imei);
        return 0;
    }

    log_warn("[signal] No carrier ID available (WiFi-only device)");
    return -1;
}

void signal_print_info(const device_info_t *dev)
{
    signal_type_t type;
    const char   *type_str;
    char          carrier_id[DEVICE_FIELD_LEN];
    int           preserve;

    if (!dev) {
        log_warn("[signal] NULL device passed to signal_print_info");
        return;
    }

    type    = signal_detect_type(dev);
    preserve = signal_can_preserve(dev);

    switch (type) {
    case SIGNAL_NONE: type_str = "WiFi-only (no cellular)"; break;
    case SIGNAL_GSM:  type_str = "GSM (IMEI only)";         break;
    case SIGNAL_MEID: type_str = "CDMA (MEID only)";        break;
    case SIGNAL_DUAL: type_str = "Dual (IMEI + MEID)";      break;
    default:          type_str = "unknown";                  break;
    }

    log_info("[signal] Type          : %s", type_str);
    log_info("[signal] IMEI          : %s",
             field_present(dev->imei) ? dev->imei : "(none)");
    log_info("[signal] MEID          : %s",
             field_present(dev->meid) ? dev->meid : "(none)");
    log_info("[signal] Can preserve  : %s", preserve ? "yes" : "no");

    if (preserve) {
        if (signal_get_carrier_id(dev, carrier_id, sizeof(carrier_id)) == 0)
            log_info("[signal] Carrier ID    : %s", carrier_id);
    }
}
