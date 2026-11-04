/*
 * path_a_ssh_internal.h -- SSH helper wrappers shared between the
 * Path A delivery and mobileactivationd-replacement flows.
 *
 * Not a public API; internal to the pipeline module.  The wrappers hide
 * the raw libssh2 session/channel lifecycle so that path_a_ssh.c only
 * deals in high-level "open / exec / scp / close" operations.
 */

#ifndef PIPELINE_PATH_A_SSH_INTERNAL_H
#define PIPELINE_PATH_A_SSH_INTERNAL_H

#include <stddef.h>
#include <sys/types.h>

/* Opaque session handle owned by path_a_ssh_helpers.c. */
typedef struct ssh_sess ssh_sess_t;

/*
 * ssh_open -- Establish a libssh2 session with password authentication.
 *
 * host/user/password must be non-NULL.  On success *sess_out is set to a
 * heap-allocated handle that the caller frees with ssh_close().  Returns
 * 0 on success, -1 on any failure (socket, handshake, auth).
 */
int ssh_open(ssh_sess_t **sess_out, const char *host, int port,
             const char *user, const char *password);

/*
 * ssh_exec -- Run a single command over SSH and capture its stdout.
 *
 * If exit_code_out is non-NULL the remote exit status is written there.
 * If stdout_buf/stdout_cap are non-zero, up to (stdout_cap-1) bytes of
 * stdout are copied; the buffer is always NUL-terminated on success.
 *
 * Returns 0 on a successful RPC round-trip (regardless of remote exit
 * code) or -1 if the channel / transport itself failed.
 */
int ssh_exec(ssh_sess_t *sess, const char *cmd, int *exit_code_out,
             char *stdout_buf, size_t stdout_cap);

/*
 * ssh_scp_upload -- Push a local file to remote_path via SCP.
 *
 * The destination inode is created with the given unix mode.  Returns 0
 * on success, -1 on any error (read failure, SCP channel rejection,
 * truncated write).
 */
int ssh_scp_upload(ssh_sess_t *sess, const char *local_path,
                   const char *remote_path, mode_t mode);

/*
 * ssh_close -- Disconnect and free a session created by ssh_open.  Safe
 * to call with a NULL handle.
 */
void ssh_close(ssh_sess_t *sess);

/*
 * ssh_global_init / ssh_global_exit -- Reference-counted libssh2_init
 * wrappers.  Helpers call them from ssh_open/ssh_close so tests do not
 * need to coordinate global init.  Not for direct use.
 */
int  ssh_global_init(void);
void ssh_global_exit(void);

#endif /* PIPELINE_PATH_A_SSH_INTERNAL_H */
