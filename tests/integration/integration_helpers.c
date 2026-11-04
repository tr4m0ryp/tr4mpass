/*
 * integration_helpers.c -- Fixture setup for Path A / Path B E2E tests.
 *
 * Responsibilities:
 *   - create temp directory + minimal blob files for each pipeline stage
 *   - set the TR4MPASS_* env vars that path_a_* / path_b_* read
 *   - build synthetic device_info_t records for A5-A11 / A12+ / WiFi-only
 *   - provide small call-log inspection helpers for ordering assertions
 *
 * Everything lives under /tmp/tr4mpass_e2e_<pid>/ so parallel test runs
 * never collide.  mock_reset_all() is intentionally NOT called here --
 * tests choose when to reset mock state relative to fixture setup.
 */

#include "integration_helpers.h"

#include "mocks/mock_control.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* 32 bytes is enough to satisfy file_readable() + SCP-upload byte counts. */
static const unsigned char E2E_BLOB[32] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
};

static int
write_blob(const char *path)
{
    FILE *f;
    size_t n;

    if (!path || !*path) {
        errno = EINVAL;
        return -1;
    }
    f = fopen(path, "wb");
    if (!f)
        return -1;
    n = fwrite(E2E_BLOB, 1, sizeof(E2E_BLOB), f);
    fclose(f);
    if (n != sizeof(E2E_BLOB)) {
        unlink(path);
        return -1;
    }
    return 0;
}

void
e2e_unset_all_env(void)
{
    unsetenv("TR4MPASS_IBSS_PATH");
    unsetenv("TR4MPASS_IBEC_PATH");
    unsetenv("TR4MPASS_DEVICETREE_PATH");
    unsetenv("TR4MPASS_RAMDISK_PATH");
    unsetenv("TR4MPASS_TRUSTCACHE_PATH");
    unsetenv("TR4MPASS_SSH_PORT");
    unsetenv("TR4MPASS_SSH_HOST");
    unsetenv("TR4MPASS_SSH_USER");
    unsetenv("TR4MPASS_SSH_PASSWORD");
    unsetenv("TR4MPASS_RAMDISK_TIMEOUT");
    unsetenv("TR4MPASS_TEST_SSH_READY");
    unsetenv("TR4MPASS_PATCHED_MAD");
    unsetenv("TR4MPASS_JBINIT_PATCHER");
    unsetenv("TR4MPASS_SIGNING_KEY");
}

int
e2e_fixture_init(e2e_fixture_t *fx)
{
    int pid = (int)getpid();

    if (!fx)
        return -1;
    memset(fx, 0, sizeof(*fx));

    snprintf(fx->base_dir, sizeof(fx->base_dir),
             "/tmp/tr4mpass_e2e_%d", pid);
    if (mkdir(fx->base_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "e2e: mkdir %s failed: %s\n",
                fx->base_dir, strerror(errno));
        return -1;
    }

    snprintf(fx->ibss,        sizeof(fx->ibss),
             "%s/iBSS.img4",       fx->base_dir);
    snprintf(fx->ibec,        sizeof(fx->ibec),
             "%s/iBEC.img4",       fx->base_dir);
    snprintf(fx->devtree,     sizeof(fx->devtree),
             "%s/DeviceTree.img4", fx->base_dir);
    snprintf(fx->trustcache,  sizeof(fx->trustcache),
             "%s/trustcache",      fx->base_dir);
    snprintf(fx->ramdisk,     sizeof(fx->ramdisk),
             "%s/ramdisk.dmg",     fx->base_dir);
    snprintf(fx->patched_mad, sizeof(fx->patched_mad),
             "%s/mobileactivationd", fx->base_dir);

    if (write_blob(fx->ibss)        != 0) return -1;
    if (write_blob(fx->ibec)        != 0) return -1;
    if (write_blob(fx->devtree)     != 0) return -1;
    if (write_blob(fx->trustcache)  != 0) return -1;
    if (write_blob(fx->ramdisk)     != 0) return -1;
    if (write_blob(fx->patched_mad) != 0) return -1;

    setenv("TR4MPASS_IBSS_PATH",       fx->ibss,        1);
    setenv("TR4MPASS_IBEC_PATH",       fx->ibec,        1);
    setenv("TR4MPASS_DEVICETREE_PATH", fx->devtree,     1);
    setenv("TR4MPASS_TRUSTCACHE_PATH", fx->trustcache,  1);
    setenv("TR4MPASS_RAMDISK_PATH",    fx->ramdisk,     1);
    setenv("TR4MPASS_PATCHED_MAD",     fx->patched_mad, 1);
    setenv("TR4MPASS_SSH_HOST",        "127.0.0.1",     1);
    setenv("TR4MPASS_SSH_PORT",        "2222",          1);
    setenv("TR4MPASS_SSH_USER",        "root",          1);
    setenv("TR4MPASS_SSH_PASSWORD",    "alpine",        1);
    setenv("TR4MPASS_RAMDISK_TIMEOUT", "2",             1);
    /* Skip the real 127.0.0.1:2222 poll -- the mock does not run sshd. */
    setenv("TR4MPASS_TEST_SSH_READY",  "1",             1);
    return 0;
}

void
e2e_fixture_teardown(e2e_fixture_t *fx)
{
    if (!fx)
        return;
    if (fx->ibss[0])        unlink(fx->ibss);
    if (fx->ibec[0])        unlink(fx->ibec);
    if (fx->devtree[0])     unlink(fx->devtree);
    if (fx->trustcache[0])  unlink(fx->trustcache);
    if (fx->ramdisk[0])     unlink(fx->ramdisk);
    if (fx->patched_mad[0]) unlink(fx->patched_mad);
    if (fx->base_dir[0])    rmdir(fx->base_dir);
    e2e_unset_all_env();
}

device_info_t
e2e_make_device_path_a(void)
{
    device_info_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.checkm8_compatible = 1;
    dev.is_dfu_mode        = 1;
    dev.cpid               = 0x8015;
    dev.ecid               = 0x001A2B3C4D5E6F70ULL;
    snprintf(dev.udid,           sizeof(dev.udid),
             "abcdef0123456789abcdef0123456789abcdef01");
    snprintf(dev.product_type,   sizeof(dev.product_type),   "iPhone10,3");
    snprintf(dev.chip_id,        sizeof(dev.chip_id),        "0x8015");
    snprintf(dev.ios_version,    sizeof(dev.ios_version),    "14.8.1");
    snprintf(dev.activation_state, sizeof(dev.activation_state),
             "Unactivated");
    snprintf(dev.serial,         sizeof(dev.serial),         "F2LXXXXXABCD");
    snprintf(dev.imei,           sizeof(dev.imei),           "352000123456789");
    snprintf(dev.hardware_model, sizeof(dev.hardware_model), "D22AP");
    snprintf(dev.device_name,    sizeof(dev.device_name),    "e2e-iphone-a11");
    snprintf(dev.chip_name,      sizeof(dev.chip_name),      "A11");
    return dev;
}

device_info_t
e2e_make_device_path_b_cellular(void)
{
    device_info_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.checkm8_compatible = 0;
    dev.is_dfu_mode        = 1;
    dev.cpid               = 0x8020;
    dev.ecid               = 0x0022334455667788ULL;
    snprintf(dev.udid,           sizeof(dev.udid),
             "11112222333344445555666677778888aaaabbbb");
    snprintf(dev.product_type,   sizeof(dev.product_type),   "iPhone11,8");
    snprintf(dev.chip_id,        sizeof(dev.chip_id),        "0x8020");
    snprintf(dev.ios_version,    sizeof(dev.ios_version),    "17.4");
    snprintf(dev.activation_state, sizeof(dev.activation_state),
             "Unactivated");
    snprintf(dev.serial,         sizeof(dev.serial),         "F2LYYYYABCD");
    snprintf(dev.imei,           sizeof(dev.imei),           "358000987654321");
    snprintf(dev.hardware_model, sizeof(dev.hardware_model), "N841AP");
    snprintf(dev.device_name,    sizeof(dev.device_name),    "e2e-iphone-a12-cell");
    snprintf(dev.chip_name,      sizeof(dev.chip_name),      "A12");
    dev.is_cellular = 1;
    return dev;
}

device_info_t
e2e_make_device_path_b_wifi(void)
{
    device_info_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.checkm8_compatible = 0;
    dev.is_dfu_mode        = 1;
    dev.cpid               = 0x8101;
    dev.ecid               = 0x0099AABBCCDDEEFFULL;
    snprintf(dev.udid,           sizeof(dev.udid),
             "aaaabbbbccccddddeeeeffff0000111122223333");
    snprintf(dev.product_type,   sizeof(dev.product_type),   "iPad11,6");
    snprintf(dev.chip_id,        sizeof(dev.chip_id),        "0x8101");
    snprintf(dev.ios_version,    sizeof(dev.ios_version),    "17.4");
    snprintf(dev.activation_state, sizeof(dev.activation_state),
             "Unactivated");
    snprintf(dev.serial,         sizeof(dev.serial),         "F2LWIFIABCD");
    dev.imei[0] = '\0';
    dev.meid[0] = '\0';
    snprintf(dev.hardware_model, sizeof(dev.hardware_model), "J217AP");
    snprintf(dev.device_name,    sizeof(dev.device_name),    "e2e-ipad-a13-wifi");
    snprintf(dev.chip_name,      sizeof(dev.chip_name),      "A13");
    dev.is_cellular = 0;
    return dev;
}

int
e2e_first_index_of(const char *needle)
{
    size_t n;
    size_t i;

    if (!needle)
        return -1;
    n = mock_get_call_count();
    for (i = 0; i < n; i++) {
        const char *line = mock_get_call_log(i);
        if (line && strstr(line, needle) != NULL)
            return (int)i;
    }
    return -1;
}

int
e2e_ordering_ok(const char *needle_a, const char *needle_b)
{
    int ia = e2e_first_index_of(needle_a);
    int ib = e2e_first_index_of(needle_b);
    if (ia < 0 || ib < 0)
        return 0;
    return ia < ib ? 1 : 0;
}
