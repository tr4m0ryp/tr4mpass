/*
 * mock_afc_mobileactivation.c -- Mocks for libimobiledevice's AFC and
 * mobileactivation service APIs.  Staging + reset lives in the sibling
 * file mock_afc_mobileactivation_ctl.c; only the interposed symbols
 * remain here.
 */

#include "mock_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/mobileactivation.h>
#include <libimobiledevice/afc.h>
#include <plist/plist.h>

/* Shared staging state (see mock_afc_mobileactivation_ctl.c). */
extern int  g_ma_activate_error;
extern int  g_ma_create_info_error;
extern char g_ma_state[64];
extern int  g_ma_start_service_error;
extern int  g_ma_start_service_failures_left;
extern int  g_ma_state_query_error;
extern int  g_ma_state_query_failures_left;

/* ------------------------------------------------------------------ */
/* mobileactivation_*                                                 */
/* ------------------------------------------------------------------ */

struct mobileactivation_client_private {
    uint32_t magic;
};

#define MA_MAGIC 0x4D414354u  /* "MACT" */

mobileactivation_error_t
mobileactivation_client_start_service(idevice_t device,
                                      mobileactivation_client_t *client,
                                      const char *label)
{
    struct mobileactivation_client_private *c;
    (void)device; (void)label;
    mock_log_append("mobileactivation_client_start_service");
    if (!client) return MOBILEACTIVATION_E_INVALID_ARG;

    /* Consume a staged startup failure if one remains. */
    if (g_ma_start_service_failures_left > 0) {
        g_ma_start_service_failures_left--;
        return (mobileactivation_error_t)g_ma_start_service_error;
    }

    c = calloc(1, sizeof(*c));
    if (!c) return MOBILEACTIVATION_E_UNKNOWN_ERROR;
    c->magic = MA_MAGIC;
    *client = c;
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_client_new(idevice_t device,
                            lockdownd_service_descriptor_t service,
                            mobileactivation_client_t *client)
{
    (void)service;
    return mobileactivation_client_start_service(device, client, "mock");
}

mobileactivation_error_t
mobileactivation_client_free(mobileactivation_client_t client)
{
    mock_log_append("mobileactivation_client_free");
    if (!client) return MOBILEACTIVATION_E_SUCCESS;
    client->magic = 0;
    free(client);
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_get_activation_state(mobileactivation_client_t client,
                                      plist_t *state)
{
    (void)client;
    mock_log_append("mobileactivation_get_activation_state");
    if (!state) return MOBILEACTIVATION_E_INVALID_ARG;

    /* Consume a staged state-query failure if one remains. */
    if (g_ma_state_query_failures_left > 0) {
        g_ma_state_query_failures_left--;
        *state = NULL;
        return (mobileactivation_error_t)g_ma_state_query_error;
    }

    *state = plist_new_string(g_ma_state);
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_create_activation_session_info(mobileactivation_client_t client,
                                                plist_t *blob)
{
    (void)client;
    mock_log_append("mobileactivation_create_activation_session_info");
    if (!blob) return MOBILEACTIVATION_E_INVALID_ARG;
    /* Return an empty dict: tests can replace via Phase 2 helpers. */
    *blob = plist_new_dict();
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_create_activation_info(mobileactivation_client_t client,
                                        plist_t *info)
{
    (void)client;
    mock_log_append("mobileactivation_create_activation_info");
    if (!info) return MOBILEACTIVATION_E_INVALID_ARG;
    if (g_ma_create_info_error != 0) {
        *info = NULL;
        return (mobileactivation_error_t)g_ma_create_info_error;
    }
    *info = plist_new_dict();
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_create_activation_info_with_session(mobileactivation_client_t client,
                                                     plist_t handshake_response,
                                                     plist_t *info)
{
    (void)client; (void)handshake_response;
    mock_log_append("mobileactivation_create_activation_info_with_session");
    if (!info) return MOBILEACTIVATION_E_INVALID_ARG;
    if (g_ma_create_info_error != 0) {
        *info = NULL;
        return (mobileactivation_error_t)g_ma_create_info_error;
    }
    *info = plist_new_dict();
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_activate(mobileactivation_client_t client,
                          plist_t activation_record)
{
    (void)client; (void)activation_record;
    mock_log_append("mobileactivation_activate");
    if (g_ma_activate_error != 0)
        return (mobileactivation_error_t)g_ma_activate_error;
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_activate_with_session(mobileactivation_client_t client,
                                       plist_t activation_record,
                                       plist_t headers)
{
    (void)client; (void)activation_record; (void)headers;
    mock_log_append("mobileactivation_activate_with_session");
    if (g_ma_activate_error != 0)
        return (mobileactivation_error_t)g_ma_activate_error;
    return MOBILEACTIVATION_E_SUCCESS;
}

mobileactivation_error_t
mobileactivation_deactivate(mobileactivation_client_t client)
{
    (void)client;
    mock_log_append("mobileactivation_deactivate");
    return MOBILEACTIVATION_E_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* afc_*                                                              */
/* ------------------------------------------------------------------ */

struct afc_client_private {
    uint32_t magic;
    uint64_t next_handle;
};

#define AFC_MAGIC 0x41464343u  /* "AFCC" */

afc_error_t afc_client_new(idevice_t device,
                           lockdownd_service_descriptor_t service,
                           afc_client_t *client)
{
    struct afc_client_private *c;
    (void)device; (void)service;
    mock_log_append("afc_client_new");
    if (!client) return AFC_E_INVALID_ARG;
    c = calloc(1, sizeof(*c));
    if (!c) return AFC_E_NO_MEM;
    c->magic       = AFC_MAGIC;
    c->next_handle = 0x1000;
    *client = c;
    return AFC_E_SUCCESS;
}

afc_error_t afc_client_start_service(idevice_t device, afc_client_t *client,
                                     const char *label)
{
    (void)label;
    return afc_client_new(device, NULL, client);
}

afc_error_t afc_client_free(afc_client_t client)
{
    mock_log_append("afc_client_free");
    if (!client) return AFC_E_SUCCESS;
    client->magic = 0;
    free(client);
    return AFC_E_SUCCESS;
}

afc_error_t afc_file_open(afc_client_t client, const char *filename,
                          afc_file_mode_t mode, uint64_t *handle)
{
    (void)mode;
    mock_log_append("afc_file_open(%s)", filename ? filename : "");
    if (!client || !handle) return AFC_E_INVALID_ARG;
    *handle = ++client->next_handle;
    return AFC_E_SUCCESS;
}

afc_error_t afc_file_close(afc_client_t client, uint64_t handle)
{
    (void)client; (void)handle;
    mock_log_append("afc_file_close");
    return AFC_E_SUCCESS;
}

afc_error_t afc_file_read(afc_client_t client, uint64_t handle,
                          char *data, uint32_t length, uint32_t *bytes_read)
{
    (void)client; (void)handle; (void)data; (void)length;
    if (bytes_read) *bytes_read = 0;
    return AFC_E_SUCCESS;
}

afc_error_t afc_file_write(afc_client_t client, uint64_t handle,
                           const char *data, uint32_t length,
                           uint32_t *bytes_written)
{
    (void)client; (void)handle; (void)data;
    mock_log_append("afc_file_write(%u)", length);
    if (bytes_written) *bytes_written = length;
    return AFC_E_SUCCESS;
}

afc_error_t afc_remove_path(afc_client_t client, const char *path)
{
    (void)client;
    mock_log_append("afc_remove_path(%s)", path ? path : "");
    return AFC_E_SUCCESS;
}

afc_error_t afc_make_directory(afc_client_t client, const char *path)
{
    (void)client;
    mock_log_append("afc_make_directory(%s)", path ? path : "");
    return AFC_E_SUCCESS;
}

afc_error_t afc_read_directory(afc_client_t client, const char *path,
                               char ***directory_information)
{
    (void)client; (void)path;
    if (directory_information) {
        char **arr = calloc(1, sizeof(char *));
        if (arr) *directory_information = arr;
        else     return AFC_E_NO_MEM;
    }
    return AFC_E_SUCCESS;
}

afc_error_t afc_get_file_info(afc_client_t client, const char *path,
                              char ***file_information)
{
    (void)client; (void)path;
    if (file_information) {
        char **arr = calloc(1, sizeof(char *));
        if (arr) *file_information = arr;
        else     return AFC_E_NO_MEM;
    }
    return AFC_E_SUCCESS;
}

afc_error_t afc_rename_path(afc_client_t client, const char *from,
                            const char *to)
{
    (void)client;
    mock_log_append("afc_rename_path(%s -> %s)",
                    from ? from : "", to ? to : "");
    return AFC_E_SUCCESS;
}

afc_error_t afc_dictionary_free(char **dictionary)
{
    int i;
    if (!dictionary) return AFC_E_SUCCESS;
    for (i = 0; dictionary[i]; i++)
        free(dictionary[i]);
    free(dictionary);
    return AFC_E_SUCCESS;
}

const char *afc_strerror(afc_error_t err)
{
    switch ((int)err) {
    case 0:  return "Success";
    default: return "Mock AFC error";
    }
}
