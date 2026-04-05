#ifndef PLIST_HELPERS_H
#define PLIST_HELPERS_H

#include <stdint.h>
#include <plist/plist.h>

/*
 * Retrieve a string value from a plist dict by key.
 * Returns a malloc'd copy the caller must free, or NULL on error.
 */
char *plist_dict_get_string(plist_t dict, const char *key);

/*
 * Retrieve a boolean value from a plist dict by key.
 * Stores result in *out (1=true, 0=false).
 * Returns 0 on success, -1 on error.
 */
int plist_dict_get_bool_val(plist_t dict, const char *key, int *out);

/*
 * Retrieve an unsigned 64-bit integer value from a plist dict by key.
 * Stores result in *out.
 * Returns 0 on success, -1 on error.
 */
int plist_dict_get_uint_val(plist_t dict, const char *key, uint64_t *out);

/*
 * Serialize a plist to an XML string.
 * *len is set to the byte length (excluding NUL).
 * Returns a plist-allocated string; caller must free with plist_mem_free().
 */
char *plist_to_xml_string(plist_t plist, uint32_t *len);

/*
 * Parse an XML plist from a buffer.
 * Returns a new plist_t the caller must free with plist_free(), or NULL on error.
 */
plist_t plist_from_xml_string(const char *xml, uint32_t len);

#endif /* PLIST_HELPERS_H */
