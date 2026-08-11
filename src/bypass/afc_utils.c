/*
 * afc_utils.c -- AFC file operations wrapper.
 *
 * Convenience layer over the libimobiledevice AFC API. AFC root on the
 * device maps to /var/mobile/Media/.
 */

#include <stdlib.h>
#include <string.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/lockdown.h>
#include "bypass/afc_utils.h"
#include "util/log.h"

/*
 * afc_err_str -- Convert an afc_error_t code to a human-readable string.
 * Named differently from afc_err_str() to avoid conflict with
 * libimobiledevice >= 1.4.0 which exports that symbol.
 */
static const char *afc_err_str(afc_error_t err)
{
    switch (err) {
    case AFC_E_SUCCESS:              return "success";
    case AFC_E_UNKNOWN_ERROR:        return "unknown error";
    case AFC_E_OP_HEADER_INVALID:    return "operation header invalid";
    case AFC_E_NO_RESOURCES:         return "no resources";
    case AFC_E_READ_ERROR:           return "read error";
    case AFC_E_WRITE_ERROR:          return "write error";
    case AFC_E_UNKNOWN_PACKET_TYPE:  return "unknown packet type";
    case AFC_E_INVALID_ARG:          return "invalid argument";
    case AFC_E_OBJECT_NOT_FOUND:     return "object not found";
    case AFC_E_OBJECT_IS_DIR:        return "object is a directory";
    case AFC_E_PERM_DENIED:          return "permission denied";
    case AFC_E_SERVICE_NOT_CONNECTED: return "service not connected";
    case AFC_E_OP_TIMEOUT:           return "operation timeout";
    case AFC_E_TOO_MUCH_DATA:        return "too much data";
    case AFC_E_END_OF_DATA:          return "end of data";
    case AFC_E_OP_NOT_SUPPORTED:     return "operation not supported";
    case AFC_E_OBJECT_EXISTS:        return "object exists";
    case AFC_E_OBJECT_BUSY:          return "object busy";
    case AFC_E_NO_SPACE_LEFT:        return "no space left";
    case AFC_E_OP_WOULD_BLOCK:       return "operation would block";
    case AFC_E_IO_ERROR:             return "I/O error";
    case AFC_E_OP_INTERRUPTED:       return "operation interrupted";
    case AFC_E_OP_IN_PROGRESS:       return "operation in progress";
    case AFC_E_INTERNAL_ERROR:       return "internal error";
    case AFC_E_MUX_ERROR:            return "mux error";
    case AFC_E_NO_MEM:               return "no memory";
    case AFC_E_NOT_ENOUGH_DATA:      return "not enough data";
    case AFC_E_DIR_NOT_EMPTY:        return "directory not empty";
    default:                         return "unknown AFC error";
    }
}

int afc_connect_service(device_info_t *dev, afc_client_t *client,
                        const char *service_name)
{
    lockdownd_service_descriptor_t svc = NULL;
    lockdownd_error_t lerr;
    afc_error_t aerr;

    if (!dev || !dev->lockdown || !client || !service_name) {
        log_error("[afc] Invalid arguments to afc_connect_service");
        return -1;
    }

    lerr = lockdownd_start_service(dev->lockdown, service_name, &svc);
    if (lerr != LOCKDOWN_E_SUCCESS || !svc) {
        log_error("[afc] Could not start service '%s': %s",
                  service_name, lockdownd_strerror(lerr));
        return -1;
    }

    aerr = afc_client_new(dev->handle, svc, client);
    lockdownd_service_descriptor_free(svc);

    if (aerr != AFC_E_SUCCESS) {
        log_error("[afc] afc_client_new failed: %s", afc_err_str(aerr));
        return -1;
    }

    return 0;
}

int afc_connect(device_info_t *dev, afc_client_t *client)
{
    return afc_connect_service(dev, client, AFC_SERVICE_NAME);
}

void afc_disconnect(afc_client_t client)
{
    if (client) {
        afc_client_free(client);
    }
}

int afc_write_file(afc_client_t client, const char *path,
                   const char *data, uint32_t len)
{
    uint64_t handle = 0;
    uint32_t written = 0;
    afc_error_t err;

    if (!client || !path || !data) {
        log_error("[afc] Invalid arguments to afc_write_file");
        return -1;
    }

    err = afc_file_open(client, path, AFC_FOPEN_WR, &handle);
    if (err != AFC_E_SUCCESS) {
        log_error("[afc] Cannot open '%s' for writing: %s",
                  path, afc_err_str(err));
        return -1;
    }

    err = afc_file_write(client, handle, data, len, &written);
    afc_file_close(client, handle);

    if (err != AFC_E_SUCCESS) {
        log_error("[afc] Write to '%s' failed: %s", path, afc_err_str(err));
        return -1;
    }

    return (int)written;
}

int afc_read_file(afc_client_t client, const char *path,
                  char *buf, uint32_t max_len)
{
    uint64_t handle = 0;
    uint32_t total_read = 0;
    afc_error_t err;

    if (!client || !path || !buf || max_len == 0) {
        log_error("[afc] Invalid arguments to afc_read_file");
        return -1;
    }

    err = afc_file_open(client, path, AFC_FOPEN_RDONLY, &handle);
    if (err != AFC_E_SUCCESS) {
        log_error("[afc] Cannot open '%s' for reading: %s",
                  path, afc_err_str(err));
        return -1;
    }

    /* Read in chunks until EOF or buffer full */
    while (total_read < max_len) {
        uint32_t bytes_read = 0;
        uint32_t chunk = max_len - total_read;
        if (chunk > 65536) chunk = 65536;

        err = afc_file_read(client, handle, buf + total_read,
                            chunk, &bytes_read);
        if (err != AFC_E_SUCCESS || bytes_read == 0)
            break;
        total_read += bytes_read;
    }

    afc_file_close(client, handle);

    if (total_read < max_len)
        buf[total_read] = '\0';
    else
        buf[max_len - 1] = '\0';

    return (int)total_read;
}

int afc_list_dir(afc_client_t client, const char *path)
{
    char **entries = NULL;
    afc_error_t err;
    int count = 0;

    if (!client || !path) {
        log_error("[afc] Invalid arguments to afc_list_dir");
        return -1;
    }

    err = afc_read_directory(client, path, &entries);
    if (err != AFC_E_SUCCESS) {
        log_error("[afc] Cannot list '%s': %s", path, afc_err_str(err));
        return -1;
    }

    log_info("[afc] Directory listing of '%s':", path);
    for (int i = 0; entries[i] != NULL; i++) {
        /* Skip . and .. */
        if (strcmp(entries[i], ".") == 0 || strcmp(entries[i], "..") == 0)
            continue;
        log_info("  %s", entries[i]);
        count++;
    }
    log_info("[afc] %d entries", count);

    afc_dictionary_free(entries);
    return count;
}

int afc_file_exists(afc_client_t client, const char *path)
{
    char **info = NULL;
    afc_error_t err;

    if (!client || !path) return -1;

    err = afc_get_file_info(client, path, &info);
    if (err == AFC_E_SUCCESS && info) {
        afc_dictionary_free(info);
        return 1;
    }

    if (err == AFC_E_OBJECT_NOT_FOUND)
        return 0;

    return -1;
}

int afc_remove_file(afc_client_t client, const char *path)
{
    afc_error_t err;

    if (!client || !path) {
        log_error("[afc] Invalid arguments to afc_remove_file");
        return -1;
    }

    err = afc_remove_path(client, path);
    if (err != AFC_E_SUCCESS) {
        log_error("[afc] Cannot remove '%s': %s", path, afc_err_str(err));
        return -1;
    }

    return 0;
}
