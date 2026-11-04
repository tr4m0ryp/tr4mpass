#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "dfu/iokit_usb.h"

#define EXEC_MAGIC 0x6578656365786563ULL

static void hexdump(const char *lbl, const uint8_t *p, int n) {
    fprintf(stderr, "%s:\n", lbl);
    for (int i = 0; i < n; i += 16) {
        fprintf(stderr, "  %04x: ", i);
        for (int j = 0; j < 16 && i+j < n; j++) fprintf(stderr, "%02x", p[i+j]);
        fprintf(stderr, "\n");
    }
}

int main(void) {
    if (iokit_usb_iface_activate() != 0) { fprintf(stderr, "activate failed\n"); return 1; }

    uint8_t req[72 + 48];
    memset(req, 0, sizeof(req));
    uint64_t *w = (uint64_t *)req;
    w[0] = EXEC_MAGIC;
    w[1] = 0x10000DAA0ULL;            /* aes_crypto_cmd */
    w[2] = 17;                        /* cmd = CBC|DEC */
    w[3] = 0x180380000ULL + 9*8;      /* dst */
    w[4] = 0x180380000ULL + 2*8;      /* src */
    w[5] = 48;                        /* len */
    w[6] = 0x20000200ULL;             /* options: 256-bit GID0 */
    for (int i = 0; i < 48; i++) req[72+i] = (uint8_t)i;  /* kbag test data */

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

    /* dump insecure_memory_base via pass-through GET (wValue=0) */
    uint8_t dump[0x80] = {0};
    r = iokit_usb_ctrl_transfer_iface(0xA1, 2, 0, 0, dump, 0x80);
    fprintf(stderr, "dump GET r=%d\n", r);
    if (r > 0) hexdump("insecure_memory_base", dump, 0x50);
    iokit_usb_iface_deactivate();
    return 0;
}
