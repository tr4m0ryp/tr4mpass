/*
 * compat/libirecovery_compat.h -- Compatibility layer for libirecovery API.
 *
 * Handles differences between old and new libirecovery versions:
 *  - Old (pre-2024): uses info->pid field
 *  - New (2024+):    uses info->cpid field, pid removed
 *
 * Also normalizes missing constants like IRECV_SEND_OPT_DFU_NOTIFY_FINISH
 * which was removed in modern versions.
 *
 * Usage:
 *  #include "compat/libirecovery_compat.h"
 *
 *  // Use this macro for device ID checks:
 *  uint32_t device_id = irecv_get_device_id(info);
 *
 *  // Use this for the send file flag:
 *  irecv_send_file(client, path, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
 */

#ifndef LIBIRECOVERY_COMPAT_H
#define LIBIRECOVERY_COMPAT_H

#include <stdint.h>
#include <libirecovery.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * DEVICE ID FIELD COMPATIBILITY
 * ============================================================================
 * Modern libirecovery uses 'cpid' (chip ID), older versions used 'pid'
 * (product ID). Both represent the device identifier.
 */

/*
 * irecv_get_device_id -- Get the device ID from irecv_device_info.
 *
 * Abstracts the difference between old (pid) and new (cpid) fields.
 * Returns 0 if info is NULL.
 */
static inline uint32_t irecv_get_device_id(const struct irecv_device_info *info)
{
    if (!info)
        return 0;

#ifdef HAVE_IRECV_DEVICE_INFO_CPID
    /* Modern API: cpid field available */
    return info->cpid;
#else
    /* Legacy API: pid field available */
    return info->pid;
#endif
}

/*
 * IRECV_DEVICE_ID_FIELD_NAME -- String name of the field in use.
 *
 * Useful for debug/error messages to show which API is being used.
 */
#ifdef HAVE_IRECV_DEVICE_INFO_CPID
    #define IRECV_DEVICE_ID_FIELD_NAME "cpid"
#else
    #define IRECV_DEVICE_ID_FIELD_NAME "pid"
#endif

/* ============================================================================
 * DFU NOTIFY FINISH FLAG COMPATIBILITY
 * ============================================================================
 * The IRECV_SEND_OPT_DFU_NOTIFY_FINISH flag was removed in modern libirecovery.
 * We define it as 0 if missing, which is the safe fallback behavior.
 */

#ifndef IRECV_SEND_OPT_DFU_NOTIFY_FINISH
    /* Modern API: flag not available, use 0 (safe fallback) */
    #define IRECV_SEND_OPT_DFU_NOTIFY_FINISH 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBIRECOVERY_COMPAT_H */
