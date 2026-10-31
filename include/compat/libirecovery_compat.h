#ifndef TP_LIBIRECOVERY_COMPAT_H
#define TP_LIBIRECOVERY_COMPAT_H

/*
 * libirecovery_compat.h -- libirecovery API drift shim.
 *
 * Older libirecovery releases lacked IRECV_SEND_OPT_DFU_NOTIFY_FINISH,
 * the send-flag that instructs irecv_send_file() to terminate a DFU
 * transfer with a zero-length packet. The Makefile probes for the
 * symbol and passes -DTP_IRECV_HAS_NOTIFY_FINISH when present.
 *
 * When absent we provide a no-op flag value and set
 * TP_IRECV_NEEDS_MANUAL_ZLP=1 so callers can send the ZLP by hand
 * after irecv_send_file() returns.
 */

#include <libirecovery.h>

#ifdef TP_IRECV_HAS_NOTIFY_FINISH
    #define TP_IRECV_NEEDS_MANUAL_ZLP 0
#else
    /*
     * The Makefile probe compiles a reference to the enum member to decide
     * whether it exists.  If we land here, the probe said "not present",
     * so we provide a no-op fallback (value 0).
     *
     * The old #ifndef guard was removed: IRECV_SEND_OPT_DFU_NOTIFY_FINISH
     * is an enum member, invisible to the preprocessor, so #ifndef could
     * never detect it.
     */
    #define IRECV_SEND_OPT_DFU_NOTIFY_FINISH 0
    #define TP_IRECV_NEEDS_MANUAL_ZLP 1
#endif

#endif /* TP_LIBIRECOVERY_COMPAT_H */
