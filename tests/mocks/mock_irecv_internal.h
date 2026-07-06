/*
 * mock_irecv_internal.h -- Shared state for libirecovery mock.
 */

#ifndef TR4MPASS_MOCK_IRECV_INTERNAL_H
#define TR4MPASS_MOCK_IRECV_INTERNAL_H

#include "mock_internal.h"

#include <stdint.h>
#include <stddef.h>

#include <libirecovery.h>

struct irecv_client_private {
    uint32_t magic;
    struct irecv_device_info info;
    char serial_buf[128];
};

#define IRECV_MAGIC 0x49525256u  /* "IRRV" */

#define MOCK_SETENV_SLOTS 16

typedef struct {
    char var[64];
    int  error;
    int  in_use;
} mock_irecv_setenv_result_t;

/* Phase 3: per-path staged errors for irecv_send_file().
 * Each slot matches via strstr() on the path argument; the first unconsumed
 * match wins and the slot is marked consumed. */
#define IRECV_SEND_FILE_STAGES 8

typedef struct {
    char path_needle[128];   /* match via strstr() against the filename */
    int  error_code;
    int  in_use;
    int  consumed;
} irecv_send_file_stage_t;

/* Phase 3: FIFO of staged irecv_open errors.  Each call to
 * irecv_open_with_ecid consumes one slot (or returns success if empty). */
#define IRECV_OPEN_ERROR_SLOTS 8

typedef struct {
    int error_code;
    int in_use;
    int consumed;
} irecv_open_error_slot_t;

extern char      g_irecv_cmd_expect[256];
extern int       g_irecv_cmd_expect_active;
extern size_t   *g_irecv_file_upload_bytes_out;
extern int       g_irecv_file_upload_active;
extern mock_irecv_setenv_result_t g_irecv_setenv_results[MOCK_SETENV_SLOTS];
extern int       g_irecv_setenv_default_error;
extern int       g_irecv_open_error;   /* backwards-compat sticky flag */
extern irecv_open_error_slot_t g_irecv_open_error_slots[IRECV_OPEN_ERROR_SLOTS];
extern irecv_send_file_stage_t g_irecv_send_file_stages[IRECV_SEND_FILE_STAGES];
extern uint32_t  g_irecv_info_pid;
extern uint64_t  g_irecv_info_ecid;
extern char      g_irecv_info_serial[128];
extern char      g_irecv_env_serial_number[256];

#endif /* TR4MPASS_MOCK_IRECV_INTERNAL_H */
