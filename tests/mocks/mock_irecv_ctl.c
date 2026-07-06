/*
 * mock_irecv_ctl.c -- Staging + reset for the libirecovery mock.
 */

#include "mock_irecv_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char      g_irecv_cmd_expect[256];
int       g_irecv_cmd_expect_active;
size_t   *g_irecv_file_upload_bytes_out;
int       g_irecv_file_upload_active;
mock_irecv_setenv_result_t g_irecv_setenv_results[MOCK_SETENV_SLOTS];
int       g_irecv_setenv_default_error;
int       g_irecv_open_error;
irecv_open_error_slot_t    g_irecv_open_error_slots[IRECV_OPEN_ERROR_SLOTS];
irecv_send_file_stage_t    g_irecv_send_file_stages[IRECV_SEND_FILE_STAGES];
uint32_t  g_irecv_info_pid  = 0x1281;
uint64_t  g_irecv_info_ecid;
char      g_irecv_info_serial[128] =
    "CPID:8020 CPRV:11 ECID:0012345678ABCDEF";
char      g_irecv_env_serial_number[256];

void mock_irecv_reset(void)
{
    int i;
    g_irecv_cmd_expect[0]         = '\0';
    g_irecv_cmd_expect_active     = 0;
    g_irecv_file_upload_bytes_out = NULL;
    g_irecv_file_upload_active    = 0;
    for (i = 0; i < MOCK_SETENV_SLOTS; i++)
        memset(&g_irecv_setenv_results[i], 0,
               sizeof(g_irecv_setenv_results[i]));
    g_irecv_setenv_default_error = 0;
    g_irecv_open_error           = 0;
    for (i = 0; i < IRECV_OPEN_ERROR_SLOTS; i++)
        memset(&g_irecv_open_error_slots[i], 0,
               sizeof(g_irecv_open_error_slots[i]));
    for (i = 0; i < IRECV_SEND_FILE_STAGES; i++)
        memset(&g_irecv_send_file_stages[i], 0,
               sizeof(g_irecv_send_file_stages[i]));
    g_irecv_info_pid             = 0x1281;
    g_irecv_info_ecid            = 0;
    snprintf(g_irecv_info_serial, sizeof(g_irecv_info_serial), "%s",
             "CPID:8020 CPRV:11 ECID:0012345678ABCDEF");
    g_irecv_env_serial_number[0] = '\0';
}

void mock_irecv_expect_command(const char *cmd_substring)
{
    if (!cmd_substring) {
        g_irecv_cmd_expect_active = 0;
        return;
    }
    snprintf(g_irecv_cmd_expect, sizeof(g_irecv_cmd_expect), "%s",
             cmd_substring);
    g_irecv_cmd_expect_active = 1;
}

void mock_irecv_expect_file_upload(size_t *bytes_sent_out)
{
    g_irecv_file_upload_bytes_out = bytes_sent_out;
    g_irecv_file_upload_active    = 1;
}

void mock_irecv_set_setenv_result(const char *var, int error_code)
{
    int i;
    if (!var) {
        g_irecv_setenv_default_error = error_code;
        return;
    }
    for (i = 0; i < MOCK_SETENV_SLOTS; i++) {
        if (!g_irecv_setenv_results[i].in_use) {
            snprintf(g_irecv_setenv_results[i].var,
                     sizeof(g_irecv_setenv_results[i].var), "%s", var);
            g_irecv_setenv_results[i].error  = error_code;
            g_irecv_setenv_results[i].in_use = 1;
            return;
        }
    }
}

void mock_irecv_set_device_info(uint32_t pid, uint64_t ecid,
                                const char *serial_string)
{
    g_irecv_info_pid  = pid;
    g_irecv_info_ecid = ecid;
    snprintf(g_irecv_info_serial, sizeof(g_irecv_info_serial), "%s",
             serial_string ? serial_string : "");
}

/*
 * mock_irecv_set_open_error -- backwards-compatible sticky open error.
 * Also queues a single entry in the FIFO so new-style callers still see
 * an error on the very first call.
 */
void mock_irecv_set_open_error(int error_code)
{
    g_irecv_open_error = error_code;
    if (error_code != 0) {
        int i;
        for (i = 0; i < IRECV_OPEN_ERROR_SLOTS; i++) {
            if (!g_irecv_open_error_slots[i].in_use) {
                g_irecv_open_error_slots[i].error_code = error_code;
                g_irecv_open_error_slots[i].in_use     = 1;
                g_irecv_open_error_slots[i].consumed   = 0;
                return;
            }
        }
    }
}

/*
 * mock_irecv_queue_open_error -- push one more staged open error onto
 * the FIFO.  Each irecv_open_with_ecid call consumes one slot; once the
 * FIFO is empty the call succeeds normally.
 */
void mock_irecv_queue_open_error(int error_code)
{
    int i;
    if (error_code == 0)
        return;
    for (i = 0; i < IRECV_OPEN_ERROR_SLOTS; i++) {
        if (!g_irecv_open_error_slots[i].in_use) {
            g_irecv_open_error_slots[i].error_code = error_code;
            g_irecv_open_error_slots[i].in_use     = 1;
            g_irecv_open_error_slots[i].consumed   = 0;
            return;
        }
    }
}

/*
 * mock_irecv_set_send_file_error -- queue an error for the next unconsumed
 * irecv_send_file() call whose path contains path_needle.  Matches are
 * FIFO by slot order; each slot fires at most once.
 */
void mock_irecv_set_send_file_error(const char *path_needle, int error_code)
{
    int i;
    if (!path_needle || error_code == 0)
        return;
    for (i = 0; i < IRECV_SEND_FILE_STAGES; i++) {
        if (!g_irecv_send_file_stages[i].in_use) {
            snprintf(g_irecv_send_file_stages[i].path_needle,
                     sizeof(g_irecv_send_file_stages[i].path_needle),
                     "%s", path_needle);
            g_irecv_send_file_stages[i].error_code = error_code;
            g_irecv_send_file_stages[i].in_use     = 1;
            g_irecv_send_file_stages[i].consumed   = 0;
            return;
        }
    }
}
