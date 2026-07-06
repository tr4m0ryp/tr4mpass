/*
 * mock_irecv_sym.c -- Interposed libirecovery entry points.
 */

#include "mock_irecv_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *irecv_strerror(irecv_error_t error)
{
    switch ((int)error) {
    case 0:  return "Success";
    case -1: return "No device found";
    case -2: return "Out of memory";
    case -3: return "Unable to connect";
    default: return "Mock irecovery error";
    }
}

const char *irecv_version(void)
{
    return "mock-1.3.1";
}

void irecv_set_debug_level(int level)
{
    (void)level;
}

irecv_error_t irecv_open_with_ecid(irecv_client_t *client, uint64_t ecid)
{
    struct irecv_client_private *c;
    int i;

    mock_log_append("irecv_open_with_ecid(ecid=0x%llx)",
                    (unsigned long long)ecid);

    if (!client)
        return (irecv_error_t)-1;

    /* FIFO of staged open errors -- consume one per call. */
    for (i = 0; i < IRECV_OPEN_ERROR_SLOTS; i++) {
        irecv_open_error_slot_t *s = &g_irecv_open_error_slots[i];
        if (s->in_use && !s->consumed) {
            s->consumed = 1;
            *client = NULL;
            return (irecv_error_t)s->error_code;
        }
    }

    /* Sticky flag (backwards compat) still takes effect when FIFO is
     * empty.  Tests that called mock_irecv_set_open_error() continue to
     * see errors on every subsequent call. */
    if (g_irecv_open_error != 0) {
        *client = NULL;
        return (irecv_error_t)g_irecv_open_error;
    }
    c = calloc(1, sizeof(*c));
    if (!c)
        return (irecv_error_t)-2;
    c->magic         = IRECV_MAGIC;
    c->info.pid      = (uint16_t)g_irecv_info_pid;
    c->info.ecid     = ecid != 0 ? ecid : g_irecv_info_ecid;
    c->info.cpid     = 0x8020;
    c->info.have_ecid = 1;
    c->info.have_cpid = 1;
    snprintf(c->serial_buf, sizeof(c->serial_buf), "%s",
             g_irecv_info_serial);
    c->info.serial_string = c->serial_buf;
    *client = c;
    return (irecv_error_t)0;
}

irecv_error_t irecv_open_with_ecid_and_attempts(irecv_client_t *client,
                                                uint64_t ecid, int attempts)
{
    (void)attempts;
    return irecv_open_with_ecid(client, ecid);
}

irecv_error_t irecv_close(irecv_client_t client)
{
    mock_log_append("irecv_close");
    if (!client)
        return (irecv_error_t)0;
    client->magic = 0;
    free(client);
    return (irecv_error_t)0;
}

irecv_error_t irecv_reset(irecv_client_t client)
{
    (void)client;
    mock_log_append("irecv_reset");
    return (irecv_error_t)0;
}

irecv_client_t irecv_reconnect(irecv_client_t client, int initial_pause)
{
    (void)initial_pause;
    mock_log_append("irecv_reconnect");
    return client;
}

const struct irecv_device_info *irecv_get_device_info(irecv_client_t client)
{
    mock_log_append("irecv_get_device_info");
    if (!client)
        return NULL;
    return &client->info;
}

irecv_error_t irecv_send_command(irecv_client_t client, const char *command)
{
    (void)client;
    mock_log_append("irecv_send_command(%s)",
                    command ? command : "(null)");

    if (g_irecv_cmd_expect_active) {
        g_irecv_cmd_expect_active = 0;
        if (!command || !strstr(command, g_irecv_cmd_expect)) {
            mock_log_append("EXPECT-FAIL: expected command containing '%s'",
                            g_irecv_cmd_expect);
            return (irecv_error_t)-1;
        }
    }
    return (irecv_error_t)0;
}

irecv_error_t irecv_send_command_breq(irecv_client_t client,
                                      const char *command, uint8_t b_request)
{
    (void)b_request;
    return irecv_send_command(client, command);
}

irecv_error_t irecv_send_file(irecv_client_t client, const char *filename,
                              unsigned int options)
{
    struct stat st;
    size_t size = 0;
    int i;

    (void)client; (void)options;
    if (filename && stat(filename, &st) == 0)
        size = (size_t)st.st_size;

    mock_log_append("irecv_send_file(%s,%zu)",
                    filename ? filename : "(null)", size);

    /* Phase 3: per-path staged errors take priority.  Each slot fires at
     * most once; path_needle is matched via strstr() on the filename. */
    if (filename) {
        for (i = 0; i < IRECV_SEND_FILE_STAGES; i++) {
            irecv_send_file_stage_t *s = &g_irecv_send_file_stages[i];
            if (!s->in_use || s->consumed)
                continue;
            if (s->path_needle[0] == '\0')
                continue;
            if (strstr(filename, s->path_needle) != NULL) {
                s->consumed = 1;
                return (irecv_error_t)s->error_code;
            }
        }
    }

    if (g_irecv_file_upload_active) {
        g_irecv_file_upload_active = 0;
        if (g_irecv_file_upload_bytes_out)
            *g_irecv_file_upload_bytes_out = size;
    }
    return (irecv_error_t)0;
}

irecv_error_t irecv_setenv(irecv_client_t client, const char *variable,
                           const char *value)
{
    int i;
    (void)client;
    mock_log_append("irecv_setenv(%s=%s)",
                    variable ? variable : "(null)",
                    value    ? value    : "(null)");

    if (variable) {
        for (i = 0; i < MOCK_SETENV_SLOTS; i++) {
            if (g_irecv_setenv_results[i].in_use &&
                strcmp(g_irecv_setenv_results[i].var, variable) == 0) {
                return (irecv_error_t)g_irecv_setenv_results[i].error;
            }
        }
        if (strcmp(variable, "serial-number") == 0 && value) {
            snprintf(g_irecv_env_serial_number,
                     sizeof(g_irecv_env_serial_number), "%s", value);
        }
    }
    return (irecv_error_t)g_irecv_setenv_default_error;
}

irecv_error_t irecv_setenv_np(irecv_client_t client, const char *variable,
                              const char *value)
{
    return irecv_setenv(client, variable, value);
}

irecv_error_t irecv_getenv(irecv_client_t client, const char *variable,
                           char **value)
{
    (void)client;
    mock_log_append("irecv_getenv(%s)", variable ? variable : "(null)");
    if (!value)
        return (irecv_error_t)0;

    if (variable && strcmp(variable, "serial-number") == 0
        && g_irecv_env_serial_number[0] != '\0') {
        *value = strdup(g_irecv_env_serial_number);
        return (irecv_error_t)0;
    }

    *value = strdup("mockvalue");
    return (irecv_error_t)0;
}

irecv_error_t irecv_saveenv(irecv_client_t client)
{
    (void)client;
    mock_log_append("irecv_saveenv");
    return (irecv_error_t)0;
}

irecv_error_t irecv_reboot(irecv_client_t client)
{
    (void)client;
    mock_log_append("irecv_reboot");
    return (irecv_error_t)0;
}
