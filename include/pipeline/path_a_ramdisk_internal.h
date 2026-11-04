/*
 * path_a_ramdisk_internal.h -- Helpers shared between the ramdisk
 * orchestrator and the SSH poll helper (path_a_ramdisk_poll.c).
 *
 * Internal to the pipeline module; not part of the public API.
 */

#ifndef PIPELINE_PATH_A_RAMDISK_INTERNAL_H
#define PIPELINE_PATH_A_RAMDISK_INTERNAL_H

/*
 * poll_ssh_ready -- wait until a TCP connection to 127.0.0.1:<port>
 * succeeds, or until timeout_sec seconds elapse.
 *
 * Returns 0 when SSH is reachable, -1 on timeout or fatal socket error.
 *
 * The environment variable TR4MPASS_TEST_SSH_READY acts as a test hook:
 *   "1"  -- return 0 immediately
 *   "0"  -- return -1 immediately
 *   any other value -- perform the real socket poll.
 */
int path_a_ramdisk_poll_ssh(int port, int timeout_sec);

#endif /* PIPELINE_PATH_A_RAMDISK_INTERNAL_H */
