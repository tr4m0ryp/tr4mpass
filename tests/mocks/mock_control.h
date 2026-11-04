/*
 * mock_control.h -- Public staging API for the tr4mpass mock library.
 *
 * Tests call these functions in their setup to:
 *   - stage canned responses for the next library call,
 *   - register expectations that the next call must match,
 *   - inspect the recorded call log after the SUT runs,
 *   - and reset all state between tests.
 *
 * The mocks themselves live in mock_*.c files in this directory.  They
 * replace the symbols normally provided by libimobiledevice, libirecovery,
 * libusb-1.0, libssh2, and libcurl when the test binary is linked without
 * those real libraries.
 *
 * NONE of this API is thread-safe.  Tests must run sequentially and call
 * mock_reset_all() in their setup hook.
 */

#ifndef TR4MPASS_MOCK_CONTROL_H
#define TR4MPASS_MOCK_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Global reset                                                       */
/* ------------------------------------------------------------------ */

/* Clear every staged value, expectation, captured call, and scratch
 * buffer in every mock.  MUST be called at the start of every test. */
void mock_reset_all(void);

/* ------------------------------------------------------------------ */
/* Call log (shared across all mocks)                                 */
/* ------------------------------------------------------------------ */

#define MOCK_LOG_MAX 512

/* Append an entry.  Used internally by mocks; exposed for ad-hoc test
 * helpers that want to record synthetic events. */
void mock_log_append(const char *fmt, ...);

/* Number of entries currently in the log. */
size_t mock_get_call_count(void);

/* Read-only pointer to entry at index; NULL if out of range. */
const char *mock_get_call_log(size_t index);

/* Return 1 if any entry contains the given substring, else 0. */
int mock_call_log_contains(const char *needle);

/* ------------------------------------------------------------------ */
/* lockdownd / idevice staging                                        */
/* ------------------------------------------------------------------ */

/*
 * Stage a value to be returned by the next matching lockdownd_get_value
 * call where (domain matches OR domain==NULL) AND key matches.
 *
 * data is duplicated; caller retains ownership of its original buffer.
 * If data_type is "string", data is interpreted as a C string.
 * If data_type is "uint", data is interpreted as a pointer to uint64_t.
 * If data_type is "data", data is a raw byte blob of len bytes.
 *
 * Multiple stagings with the same (domain,key) are allowed; mocks
 * return them in FIFO order.
 */
void mock_lockdownd_set_value(const char *domain, const char *key,
                              const char *data_type,
                              const void *data, size_t len);

/* Configure whether idevice_get_device_list returns a list with
 * `count` fake UDID entries, or zero if count==0.  The UDIDs returned
 * are synthetic ("MOCKUDID0", "MOCKUDID1", ...).  */
void mock_idevice_set_device_list(int count);

/* Force the next lockdownd_client_new_with_handshake call to fail
 * with the given error code (default 0 = LOCKDOWN_E_SUCCESS). */
void mock_lockdownd_set_handshake_error(int error_code);

/* ------------------------------------------------------------------ */
/* libirecovery staging                                               */
/* ------------------------------------------------------------------ */

/*
 * Assert that the NEXT irecv_send_command call receives a string
 * matching cmd_substring (substring match).  If it does not, the
 * call is recorded as a failure and the mock returns an error code.
 * Only one expectation may be active at a time.
 */
void mock_irecv_expect_command(const char *cmd_substring);

/*
 * Assert that the NEXT irecv_send_file call uploads a file.  On the
 * call, *bytes_sent_out is set to the number of bytes the SUT attempted
 * to transfer.  bytes_sent_out may be NULL if the test does not care.
 */
void mock_irecv_expect_file_upload(size_t *bytes_sent_out);

/*
 * Configure the return value of irecv_setenv for a specific variable
 * name (exact match).  Call with var=NULL to set the default.
 */
void mock_irecv_set_setenv_result(const char *var, int error_code);

/*
 * Configure the synthetic irecv_device_info returned by
 * irecv_get_device_info.  pid=0x1281 simulates recovery mode,
 * pid=0x1227 simulates DFU mode, etc.
 */
void mock_irecv_set_device_info(uint32_t pid, uint64_t ecid,
                                const char *serial_string);

/*
 * Force every subsequent irecv_open_* call to fail with the given error
 * code.  This is a sticky flag kept for backwards compatibility; new code
 * should prefer mock_irecv_queue_open_error() for FIFO semantics.
 */
void mock_irecv_set_open_error(int error_code);

/*
 * Push one more staged open error onto an 8-slot FIFO.  Each call to
 * irecv_open_with_ecid consumes one slot (or returns success if the FIFO
 * is empty and no sticky flag is set).
 */
void mock_irecv_queue_open_error(int error_code);

/*
 * Force the next irecv_send_file() call whose path contains
 * path_needle (strstr match) to return error_code.  Up to 8 independent
 * stagings may be queued; each fires at most once.
 */
void mock_irecv_set_send_file_error(const char *path_needle, int error_code);

/* ------------------------------------------------------------------ */
/* libusb staging                                                     */
/* ------------------------------------------------------------------ */

/*
 * Stage a response for the next libusb_control_transfer call.
 * If data_len>0 and data!=NULL, the data is copied into the caller's
 * buffer (for IN transfers).  Return code is the value libusb returns.
 */
void mock_libusb_stage_control_transfer(int return_code,
                                        const void *data, size_t data_len);

/*
 * Configure libusb_get_device_list to return `count` synthetic devices
 * with the given (vid,pid) pair.
 */
void mock_libusb_set_device_list(int count, uint16_t vid, uint16_t pid);

/* ------------------------------------------------------------------ */
/* libssh2 staging                                                    */
/* ------------------------------------------------------------------ */

/*
 * Stage a canned response for the next libssh2_channel_exec call that
 * contains command_substring.  exit_code is what the channel reports,
 * stdout_data is fed to libssh2_channel_read when the SUT reads it.
 *
 * stdout_data is duplicated; caller retains ownership.
 */
void mock_ssh_set_exec_response(const char *command_substring,
                                int exit_code,
                                const void *stdout_data, size_t stdout_len);

/*
 * Capture the destination path and payload of the next SCP upload
 * (libssh2_scp_send*).  On the call:
 *   *path_out is set to a newly allocated string that the TEST must free
 *   *bytes_out is set to the number of bytes the SUT wrote
 * Either argument may be NULL if the test does not need it.
 */
void mock_ssh_expect_scp(char **path_out, size_t *bytes_out);

/*
 * Force the next libssh2_userauth_password call to fail (0=success,
 * non-zero=error).
 */
void mock_ssh_set_auth_result(int error_code);

/*
 * Force the next libssh2_scp_send*() call to return a NULL channel.  The
 * SUT's ssh_scp_upload helper treats that as a transport failure and
 * returns -1.  The flag is self-clearing after the next SCP attempt so
 * tests that want to fail only one SCP out of several stay predictable.
 */
void mock_ssh_force_scp_error(int err);

/* ------------------------------------------------------------------ */
/* hardware stub staging (mock_hw_stubs.c)                             */
/* ------------------------------------------------------------------ */

/*
 * Force the checkm8 stub to return the given value.  Default is 0 (success).
 * Pass -1 to simulate an stage-stage failure.
 */
void mock_checkm8_set_stage_result(int result);

/*
 * Force the checkm8_verify stub to return the given value.  Default
 * is 1 (processed).  Pass 0 to simulate a non-processed device after stage; -1
 * for a query error.
 */
void mock_checkm8_set_verify_processed_result(int result);

/* ------------------------------------------------------------------ */
/* mobileactivation staging (mock_afc_mobileactivation.c)              */
/* ------------------------------------------------------------------ */

/*
 * Stage an error code to be returned by mobileactivation_activate() /
 * mobileactivation_activate_with_session() on the next call.  Used by
 * Path B tests to simulate the SEP nonce barrier documented in
 * 26.3-compatibility condition.md.  Pass 0 to clear.
 */
void mock_mobileactivation_set_activate_error(int error_code);

/*
 * Stage an error code for mobileactivation_create_activation_info() /
 * mobileactivation_create_activation_info_with_session().  Used to
 * simulate server-side rejection of session handshake.
 */
void mock_mobileactivation_set_create_info_error(int error_code);

/*
 * Stage the next activation state returned by
 * mobileactivation_get_activation_state().  Default is "Activated".
 * Pass NULL to reset to default.
 */
void mock_mobileactivation_set_state(const char *state);

/* ------------------------------------------------------------------ */
/* libcurl staging                                                    */
/* ------------------------------------------------------------------ */

/*
 * Stage a response for any curl_easy_perform whose URL (set via
 * CURLOPT_URL) contains url_substring.  The SUT's write callback will
 * be invoked with the staged body; CURLINFO_RESPONSE_CODE reports
 * http_code.  Body is duplicated.
 *
 * Multiple stagings may coexist; the first matching substring wins.
 */
void mock_curl_set_response(const char *url_substring, long http_code,
                            const void *body, size_t body_len);

/* Return number of times curl_easy_perform has been called. */
int mock_curl_get_perform_count(void);

/* Return the last URL passed to curl_easy_setopt(CURLOPT_URL). */
const char *mock_curl_get_last_url(void);

#ifdef __cplusplus
}
#endif

#endif /* TR4MPASS_MOCK_CONTROL_H */
