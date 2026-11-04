#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "dfu/iokit_usb.h"

#define EXEC_MAGIC 0x6578656365786563ULL
#define DONE_MAGIC 0x646F6E65646F6E65ULL

int main(void) {
    if (iokit_usb_iface_activate() != 0) { fprintf(stderr, "activate failed\n"); return 1; }

    /* minimal exec probe: memc-free no-op test -- write EXEC cmd that
     * just returns DONE: use memcpy cmd with len 0 */
    struct {
        uint64_t magic, func;
        uint64_t x[7];
    } exec;
    uint8_t req[72 + 48];
    memset(req, 0, sizeof(req));
    memset(&exec, 0, sizeof(exec));
    exec.magic = EXEC_MAGIC;
    exec.func = 0x10000DAA0ULL;             /* aes_crypto_cmd */
    exec.x[0] = 17;                          /* CBC|DEC */
    exec.x[1] = 0x180380000ULL + 9*8;        /* dst */
    exec.x[2] = 0x180380000ULL + 2*8;        /* src */
    exec.x[3] = 48;
    exec.x[4] = 0x20000200ULL;
    memcpy(req, &exec, 72);
    /* fill data area with test pattern (will be AES-decrypted in place) */
    for (int i = 0; i < 48; i++) req[72+i] = (uint8_t)i;

    int r = iokit_usb_ctrl_transfer_iface(0x21, 1, 0, 0, req, sizeof(req));
    fprintf(stderr, "DNLOAD r=%d\n", r);
    uint8_t zeros[16] = {0};
    r = iokit_usb_ctrl_transfer_iface(0x21, 1, 0, 0, zeros, 16);
    fprintf(stderr, "suffix r=%d\n", r);
    r = iokit_usb_ctrl_transfer_iface(0x21, 1, 0, 0, NULL, 0);
    fprintf(stderr, "zlp r=%d\n", r);
    for (int i = 0; i < 3; i++) {
        uint8_t st[6] = {0};
        r = iokit_usb_ctrl_transfer_iface(0xA1, 3, 0, 0, st, 6);
        fprintf(stderr, "status[%d] r=%d state=%u\n", i, r, r==6?st[4]:0);
    }
    uint8_t rsp[80] = {0};
    r = iokit_usb_ctrl_transfer_iface(0xA1, 2, 0xFFFF, 0, rsp, 80);
    fprintf(stderr, "exec GET r=%d\n", r);
    if (r > 0) {
        uint64_t m, ret;
        memcpy(&m, rsp, 8); memcpy(&ret, rsp+8, 8);
        fprintf(stderr, "magic=%#llx ret=%lld data0..7=", (unsigned long long)m, (long long)ret);
        for (int i = 0; i < 8; i++) fprintf(stderr, "%02x", rsp[16+i]);
        fprintf(stderr, "\n");
        if (m == DONE_MAGIC) fprintf(stderr, "DONE_MAGIC seen -- CHANNEL WORKS\n");
    }
    iokit_usb_iface_deactivate();
    return 0;
}
