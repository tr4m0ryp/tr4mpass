/*
 * path_a_ssh_helpers.c -- Thin libssh2 wrappers used by path_a_ssh.c.
 *
 * Encapsulates socket setup, handshake, password auth, command exec,
 * and SCP uploads.  ssh_sess_t is opaque to callers.
 */

#include "bypass/path_a_ssh_internal.h"
#include "util/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libssh2.h>

struct ssh_sess {
    int              sock;
    LIBSSH2_SESSION *session;
};

/* Reference-counted global init so multiple ssh_open calls are safe. */
static int g_ssh_init_refcount;

int
ssh_global_init(void)
{
    if (g_ssh_init_refcount == 0) {
        int rc = libssh2_init(0);
        if (rc != 0) {
            log_error("path_a_ssh: libssh2_init failed (rc=%d)", rc);
            return -1;
        }
    }
    g_ssh_init_refcount++;
    return 0;
}

void
ssh_global_exit(void)
{
    if (g_ssh_init_refcount <= 0)
        return;
    g_ssh_init_refcount--;
    if (g_ssh_init_refcount == 0)
        libssh2_exit();
}

static int
ssh_tcp_connect(const char *host, int port)
{
#ifdef TEST_MODE
    /* In-process libssh2 mock: no real sshd on the other end.  Hand
     * back a harmless fd the mock never touches. */
    int sock;
    (void)host; (void)port;
    sock = dup(0);
    return (sock >= 0) ? sock : 0;
#else
    struct sockaddr_in sa;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_error("path_a_ssh: socket() failed: %s", strerror(errno));
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        log_error("path_a_ssh: invalid host '%s'", host);
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        log_error("path_a_ssh: connect(%s:%d) failed: %s",
                  host, port, strerror(errno));
        close(sock);
        return -1;
    }
    return sock;
#endif
}

int
ssh_open(ssh_sess_t **sess_out, const char *host, int port,
         const char *user, const char *password)
{
    ssh_sess_t *s;

    if (!sess_out || !host || !user || !password) {
        log_error("path_a_ssh: ssh_open: NULL argument");
        return -1;
    }
    *sess_out = NULL;

    log_debug("path_a_ssh: ssh_open: initializing libssh2");
    if (ssh_global_init() != 0)
        return -1;

    s = calloc(1, sizeof(*s));
    if (!s) {
        ssh_global_exit();
        return -1;
    }
    s->sock = -1;

    log_debug("path_a_ssh: ssh_open: TCP connect to %s:%d", host, port);
    s->sock = ssh_tcp_connect(host, port);
    if (s->sock < 0)
        goto fail;
    log_debug("path_a_ssh: ssh_open: TCP connected (fd=%d)", s->sock);

    s->session = libssh2_session_init();
    if (!s->session) {
        log_error("path_a_ssh: libssh2_session_init failed");
        goto fail;
    }

    libssh2_session_set_blocking(s->session, 1);

    log_debug("path_a_ssh: ssh_open: starting SSH handshake");
    if (libssh2_session_handshake(s->session, s->sock) != 0) {
        log_error("path_a_ssh: handshake failed on %s:%d", host, port);
        goto fail;
    }
    log_debug("path_a_ssh: ssh_open: handshake complete");

    /* Host-key verification intentionally skipped: the target is always
     * the local device over USB (localhost / usbmuxd), not a remote
     * server.  A MITM between the host and its own USB bus is outside
     * the threat model. */

    log_debug("path_a_ssh: ssh_open: authenticating user '%s'", user);
    if (libssh2_userauth_password(s->session, user, password) != 0) {
        log_error("path_a_ssh: password auth failed for user '%s'", user);
        goto fail;
    }

    log_info("path_a_ssh: ssh_open: session established to %s:%d as '%s'",
             host, port, user);
    *sess_out = s;
    return 0;

fail:
    if (s->session) {
        libssh2_session_disconnect(s->session, "open-fail");
        libssh2_session_free(s->session);
    }
    if (s->sock >= 0)
        close(s->sock);
    free(s);
    ssh_global_exit();
    return -1;
}

static int
ssh_drain(LIBSSH2_CHANNEL *ch, char *stdout_buf, size_t stdout_cap)
{
    size_t total = 0;

    if (stdout_buf && stdout_cap > 0) {
        char chunk[512];
        ssize_t n;
        while ((n = libssh2_channel_read(ch, chunk, sizeof(chunk))) > 0) {
            if (total + (size_t)n >= stdout_cap)
                n = (ssize_t)(stdout_cap - total - 1);
            if (n <= 0) break;
            memcpy(stdout_buf + total, chunk, (size_t)n);
            total += (size_t)n;
            if (total + 1 >= stdout_cap) break;
        }
        if (n < 0) return -1;
        stdout_buf[total] = '\0';
    } else {
        char drain[256];
        ssize_t rc;
        while ((rc = libssh2_channel_read(ch, drain, sizeof(drain))) > 0)
            ;
        if (rc < 0) return -1;
    }
    return 0;
}

int
ssh_exec(ssh_sess_t *sess, const char *cmd, int *exit_code_out,
         char *stdout_buf, size_t stdout_cap)
{
    LIBSSH2_CHANNEL *ch;
    int exit_status;

    if (!sess || !sess->session || !cmd) {
        log_error("path_a_ssh: ssh_exec: invalid arguments");
        return -1;
    }

    log_debug("path_a_ssh: ssh_exec: opening channel for '%s'", cmd);
    ch = libssh2_channel_open_session(sess->session);
    if (!ch) {
        log_error("path_a_ssh: channel_open_session failed");
        return -1;
    }
    if (libssh2_channel_exec(ch, cmd) != 0) {
        log_error("path_a_ssh: channel_exec failed for '%s'", cmd);
        libssh2_channel_free(ch);
        return -1;
    }

    log_debug("path_a_ssh: ssh_exec: draining output");
    if (ssh_drain(ch, stdout_buf, stdout_cap) != 0) {
        log_error("path_a_ssh: channel_read failed");
        libssh2_channel_close(ch);
        libssh2_channel_free(ch);
        return -1;
    }

    libssh2_channel_close(ch);
    exit_status = libssh2_channel_get_exit_status(ch);
    if (exit_code_out)
        *exit_code_out = exit_status;
    log_debug("path_a_ssh: ssh_exec: '%s' exit=%d", cmd, exit_status);
    libssh2_channel_free(ch);
    return 0;
}

static int
ssh_scp_stream(LIBSSH2_CHANNEL *ch, int fd)
{
    unsigned char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            log_error("path_a_ssh: read failed: %s", strerror(errno));
            return -1;
        }
        if (n == 0)
            return 0;
        {
            unsigned char *p = buf;
            ssize_t left = n;
            while (left > 0) {
                ssize_t w = libssh2_channel_write(ch, (char *)p, (size_t)left);
                if (w < 0) {
                    log_error("path_a_ssh: channel_write failed (%zd)", w);
                    return -1;
                }
                p    += w;
                left -= w;
            }
        }
    }
}

int
ssh_scp_upload(ssh_sess_t *sess, const char *local_path,
               const char *remote_path, mode_t mode)
{
    LIBSSH2_CHANNEL *ch;
    struct stat st;
    int fd = -1;
    int ok = -1;

    if (!sess || !sess->session || !local_path || !remote_path) {
        log_error("path_a_ssh: ssh_scp_upload: invalid arguments");
        return -1;
    }
    if (stat(local_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        log_error("path_a_ssh: cannot stat regular file '%s'", local_path);
        return -1;
    }
    log_debug("path_a_ssh: scp_upload: '%s' -> '%s' (%lld bytes, mode=%03o)",
              local_path, remote_path, (long long)st.st_size, (unsigned)(mode & 0777));
    fd = open(local_path, O_RDONLY);
    if (fd < 0) {
        log_error("path_a_ssh: open('%s') failed: %s",
                  local_path, strerror(errno));
        return -1;
    }

    ch = libssh2_scp_send64(sess->session, remote_path, (int)(mode & 0777),
                            (libssh2_int64_t)st.st_size, 0, 0);
    if (!ch) {
        log_error("path_a_ssh: scp_send64('%s') failed", remote_path);
        close(fd);
        return -1;
    }

    ok = ssh_scp_stream(ch, fd);

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    close(fd);
    if (ok == 0)
        log_debug("path_a_ssh: scp_upload: '%s' transferred successfully", remote_path);
    else
        log_error("path_a_ssh: scp_upload: stream transfer failed for '%s'", remote_path);
    return ok;
}

void
ssh_close(ssh_sess_t *sess)
{
    if (!sess)
        return;
    log_debug("path_a_ssh: ssh_close: disconnecting session");
    if (sess->session) {
        libssh2_session_disconnect(sess->session, "goodbye");
        libssh2_session_free(sess->session);
    }
    if (sess->sock >= 0)
        close(sess->sock);
    free(sess);
    ssh_global_exit();
    log_debug("path_a_ssh: ssh_close: done");
}
