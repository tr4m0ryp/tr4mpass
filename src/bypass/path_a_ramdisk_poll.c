/*
 * path_a_ramdisk_poll.c -- SSH readiness polling for the Path A ramdisk.
 *
 * Split out of path_a_ramdisk.c to keep each translation unit under the
 * 300-line project cap. Provides path_a_ramdisk_poll_ssh(), which waits
 * for 127.0.0.1:<port> to accept a TCP connection after bootx.
 *
 * A test hook (TR4MPASS_TEST_SSH_READY) lets the unit tests exercise
 * success and timeout paths without opening real sockets.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "bypass/path_a_ramdisk_internal.h"
#include "util/log.h"

#define SSH_POLL_INTERVAL_SEC 2

static int
poll_ssh_once(int port)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        close(fd);
        return 0;
    }
    close(fd);
    return -1;
}

int
path_a_ramdisk_poll_ssh(int port, int timeout_sec)
{
    const char *test_hook;
    time_t start, now;

    test_hook = getenv("TR4MPASS_TEST_SSH_READY");
    if (test_hook && test_hook[0] != '\0') {
        if (strcmp(test_hook, "1") == 0) {
            log_info("path_a_ramdisk: test hook reports SSH ready");
            return 0;
        }
        log_error("path_a_ramdisk: test hook reports SSH timeout");
        return -1;
    }

    log_info("path_a_ramdisk: polling SSH on port %d (timeout %ds)",
             port, timeout_sec);

    start = time(NULL);
    for (;;) {
        if (poll_ssh_once(port) == 0) {
            now = time(NULL);
            log_info("path_a_ramdisk: SSH port %d is up (after %lds)",
                     port, (long)(now - start));
            return 0;
        }
        now = time(NULL);
        if ((long)(now - start) >= (long)timeout_sec) {
            log_error("path_a_ramdisk: SSH on port %d did not come up"
                      " within %ds", port, timeout_sec);
            return -1;
        }
        log_debug("path_a_ramdisk: SSH poll: port %d not ready, %lds / %ds",
                  port, (long)(now - start), timeout_sec);
        sleep(SSH_POLL_INTERVAL_SEC);
    }
}
