/*
 * test_path_b_bootimg.c -- IPSW/iBSS resolver tests for Path B.
 */

#include "test_framework.h"
#include "bypass/path_b_bootimg.h"
#include "device/device.h"
#include "util/env_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int file_readable(const char *path)
{
    return path && path[0] && access(path, R_OK) == 0;
}

static void reset_bootimg_env(void)
{
    unsetenv(TR4MPASS_ENV_IBSS_PATH);
    unsetenv(TR4MPASS_ENV_IPSW_PATH);
}

static void test_bootimg_source_names(void)
{
    ASSERT_STREQ(path_b_bootimg_source_name(PATH_B_BOOTIMG_DIRECT),
                 "TR4MPASS_IBSS_PATH");
    ASSERT_STREQ(path_b_bootimg_source_name(PATH_B_BOOTIMG_FROM_IPSW),
                 "TR4MPASS_IPSW_PATH");
    ASSERT_STREQ(path_b_bootimg_source_name(PATH_B_BOOTIMG_AUTODETECT),
                 "auto-detected IPSW");
    ASSERT_STREQ(path_b_bootimg_source_name(PATH_B_BOOTIMG_NONE), "none");
}

static void test_resolve_direct_ibss(void)
{
    char tmpl[] = "/tmp/tr4mpass_ibss_testXXXXXX";
    device_info_t dev;
    path_b_bootimg_result_t out;
    int fd;
    int rc;

    memset(&dev, 0, sizeof(dev));
    reset_bootimg_env();

    fd = mkstemp(tmpl);
    ASSERT_GT(fd, 0);
    close(fd);

    setenv(TR4MPASS_ENV_IBSS_PATH, tmpl, 1);
    rc = path_b_resolve_ibss(&dev, &out);
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(out.path, tmpl);
    ASSERT_EQ((int)out.source, (int)PATH_B_BOOTIMG_DIRECT);

    unlink(tmpl);
    reset_bootimg_env();
}

static void test_resolve_missing_returns_manual(void)
{
    device_info_t dev;
    path_b_bootimg_result_t out;
    int rc;

    memset(&dev, 0, sizeof(dev));
    reset_bootimg_env();

    rc = path_b_resolve_ibss(&dev, &out);
    ASSERT_EQ(rc, 1);
    ASSERT_EQ((int)out.source, (int)PATH_B_BOOTIMG_NONE);
    ASSERT_STREQ(out.path, "");
}

static void test_extract_ibss_from_ipsw_zip(void)
{
    char dir[] = "/tmp/tr4mpass_ipsw_testXXXXXX";
    char ipsw[512];
    char cmd[1024];
    device_info_t dev;
    path_b_bootimg_result_t out;
    int rc;

    memset(&dev, 0, sizeof(dev));
    dev.cpid = 0x8020;
    reset_bootimg_env();

    ASSERT_NOTNULL(mkdtemp(dir));

    if (snprintf(cmd, sizeof(cmd),
                 "mkdir -p '%s/Firmware/dfu' && "
                 "printf 'FAKEIBSS' > '%s/Firmware/dfu/iBSS.j71ap.RELEASE.img4' && "
                 "(cd '%s' && zip -qr ipsw.zip Firmware)",
                 dir, dir, dir) >= (int)sizeof(cmd)) {
        ASSERT(0);
        return;
    }

    if (system(cmd) != 0) {
        printf("  [skip] unzip/zip not available for IPSW extraction test\n");
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
        system(cmd);
        return;
    }

    if (snprintf(ipsw, sizeof(ipsw), "%s/ipsw.zip", dir) >= (int)sizeof(ipsw)) {
        ASSERT(0);
        return;
    }

    setenv(TR4MPASS_ENV_IPSW_PATH, ipsw, 1);
    rc = path_b_resolve_ibss(&dev, &out);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ((int)out.source, (int)PATH_B_BOOTIMG_FROM_IPSW);
    ASSERT_EQ(file_readable(out.path), 1);

    unlink(out.path);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    system(cmd);
    reset_bootimg_env();
}

void run_path_b_bootimg_tests(void)
{
    printf("--- Section 12: Path B boot image resolver ---\n");
    test_bootimg_source_names();
    test_resolve_direct_ibss();
    test_resolve_missing_returns_manual();
    test_extract_ibss_from_ipsw_zip();
}
