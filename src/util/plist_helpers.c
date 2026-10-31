#include <stdlib.h>
#include <string.h>
#include "util/plist_helpers.h"

char *plist_dict_get_string(plist_t dict, const char *key)
{
    plist_t node;
    char *val = NULL;

    if (!dict || !key)
        return NULL;

    node = plist_dict_get_item(dict, key);
    if (!node || plist_get_node_type(node) != PLIST_STRING)
        return NULL;

    plist_get_string_val(node, &val);
    if (!val)
        return NULL;

    /* libplist owns val; hand the caller an independent copy. */
    char *copy = strdup(val);
    plist_mem_free(val);
    return copy;
}

int plist_dict_get_bool_val(plist_t dict, const char *key, int *out)
{
    plist_t node;
    uint8_t b = 0;

    if (!dict || !key || !out)
        return -1;

    node = plist_dict_get_item(dict, key);
    if (!node || plist_get_node_type(node) != PLIST_BOOLEAN)
        return -1;

    plist_get_bool_val(node, &b);
    *out = (int)b;
    return 0;
}

int plist_dict_get_uint_val(plist_t dict, const char *key, uint64_t *out)
{
    plist_t node;

    if (!dict || !key || !out)
        return -1;

    node = plist_dict_get_item(dict, key);
    if (!node || plist_get_node_type(node) != PLIST_UINT)
        return -1;

    plist_get_uint_val(node, out);
    return 0;
}

char *plist_to_xml_string(plist_t plist, uint32_t *len)
{
    char *xml = NULL;
    uint32_t xml_len = 0;

    if (!plist)
        return NULL;

    plist_to_xml(plist, &xml, &xml_len);
    if (!xml)
        return NULL;

    if (len)
        *len = xml_len;
    return xml;
}

plist_t plist_from_xml_string(const char *xml, uint32_t len)
{
    plist_t result = NULL;

    if (!xml || len == 0)
        return NULL;

    plist_from_xml(xml, len, &result);
    return result;
}
