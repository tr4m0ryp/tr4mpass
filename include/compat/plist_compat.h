#ifndef TP_PLIST_COMPAT_H
#define TP_PLIST_COMPAT_H

/*
 * plist_compat.h -- libplist API drift shim.
 *
 * libplist 2.3.0 introduced plist_mem_free() and expanded
 * plist_from_memory() to a 4-arg form that also reports the parsed
 * format. Older distros still ship 2.2.x, so we detect both at build
 * time. The Makefile runs compile canaries and passes
 * -DTP_PLIST_HAS_MEM_FREE / -DTP_PLIST_HAS_FMT_ARG when the installed
 * header provides them; the header falls back otherwise.
 *
 * Callers should:
 *   - free plist-allocated buffers via plist_mem_free() (falls back
 *     to free() when the symbol is unavailable)
 *   - parse in-memory plists via tp_plist_from_memory(), which maps
 *     to the 3-arg or 4-arg form as detected at build time.
 */

#include <stdint.h>
#include <plist/plist.h>
#include <stdlib.h>

#if !defined(TP_PLIST_HAS_MEM_FREE)
    /* Pre-2.3: no plist_mem_free(); libplist used plain malloc/free.
     * plist_mem_free is a function in libplist, not a macro, so the flag
     * alone decides whether this fallback applies. */
    #define plist_mem_free(p) free(p)
#endif

static inline void tp_plist_mem_free(void *p)
{
    if (p) plist_mem_free(p);
}

#ifdef TP_PLIST_HAS_FMT_ARG
    /* 4-arg form: (data, length, out, format_out). */
    static inline int tp_plist_from_memory(const char *data,
                                           uint32_t length,
                                           plist_t *out,
                                           plist_format_t *fmt_out)
    {
        return (int)plist_from_memory(data, length, out, fmt_out);
    }
#else
    /*
     * The 3-arg plist_from_memory() does not exist in libplist 2.3+.
     * If TP_PLIST_HAS_FMT_ARG was not set, the installed libplist is
     * too old or the Makefile canary failed.  Fail loudly at compile
     * time instead of producing a cryptic argument-count error.
     */
    #error "plist_from_memory 3-arg fallback not supported -- rebuild with libplist >= 2.3 (TP_PLIST_HAS_FMT_ARG)"
#endif

#endif /* TP_PLIST_COMPAT_H */
