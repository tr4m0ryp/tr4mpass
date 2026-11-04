/*
 * dfu_boot.c -- boot an image on a checkm8-pwned DFU runtime (gaster payload).
 *
 * Background (sources, 2026-11-01):
 * - Our E19 exploit loads gaster's `handle_checkm8_request` payload, which
 *   hooks dfu_handle_request and implements the STOCK DFU state machine
 *   without signature checks (gaster.c, 0x7ff/gaster master).
 * - gaster's own image-load sequence (dfu_send_data + dfu_finish_eof +
 *   dfu_set_state_wait_reset in gaster.c):
 *       1. chunked DNLOAD (0x21/1), DFU_MAX_TRANSFER_SZ = 0x800 per chunk
 *       2. DNLOAD of 16 zero bytes (DFU_FILE_SUFFIX_LEN, "fake suffix")
 *       3. zero-length DNLOAD (ZLP)
 *       4. 3x GETSTATUS (0xA1/3): expect states 6 (MANIFEST_SYNC),
 *          7 (MANIFEST), 8 (MANIFEST_WAIT_RESET)
 *       5. USB device reset -> uploaded image boots
 * - Evidence chain for our device: irecovery -f (stock-ish sequence, no
 *   suffix, wValue=packets ZLP, 2x GETSTATUS) uploaded 100% and never
 *   booted (journal E20/E21); libopenra1n's 0x21/4 trigger (palera1n)
 *   also did nothing because that hook belongs to palera1n's payload,
 *   not gaster's (journal E22).
 *
 * Usage: dfu_boot <file.im4p>
 * Exit 0 on success. Logs to stderr.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libirecovery.h>

#define CHUNK 0x800
#define DFU_FILE_SUFFIX_LEN 16

static int ctrl(irecv_client_t client, uint8_t rt, uint8_t req,
                uint16_t wv, uint16_t wi, unsigned char *data,
                uint16_t len)
{
	return irecv_usb_control_transfer(client, rt, req, wv, wi, data,
	                                  len, 5000);
}

static int get_status(irecv_client_t client, unsigned char st[6])
{
	return ctrl(client, 0xA1, 3, 0, 0, st, 6);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <file.im4p>\n", argv[0]);
		return 2;
	}

	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror("fopen");
		return 2;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = malloc((size_t)sz);
	if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		fprintf(stderr, "read failed\n");
		return 2;
	}
	fclose(f);
	fprintf(stderr, "payload: %s (%ld bytes)\n", argv[1], sz);

	irecv_client_t client = NULL;
	irecv_error_t err = irecv_open_with_ecid(&client, 0);
	if (err != IRECV_E_SUCCESS) {
		fprintf(stderr, "open failed: %d\n", err);
		return 1;
	}
	int mode = 0;
	irecv_error_t merr = irecv_get_mode(client, &mode);
	fprintf(stderr, "device mode: %#x (err=%d)\n", mode, merr);
	if (mode != IRECV_K_DFU_MODE && mode != IRECV_K_WTF_MODE) {
		fprintf(stderr, "device not in DFU\n");
		irecv_close(client);
		return 1;
	}

	/* DNLOAD chunk loop -- identical to checkm8_boot_pongo */
	long off = 0;
	while (off < sz) {
		long chunk = (sz - off > CHUNK) ? CHUNK : (sz - off);
		int r = ctrl(client, 0x21, 1, 0, 0, buf + off, (uint16_t)chunk);
		if (r < 0) {
			fprintf(stderr, "DNLOAD failed at %ld (r=%d)\n", off, r);
			irecv_close(client);
			return 1;
		}
		off += chunk;
	}
	fprintf(stderr, "uploaded %ld bytes\n", off);

	/* gaster dfu_finish_eof: 16-byte zero suffix, then ZLP */
	unsigned char zeros[DFU_FILE_SUFFIX_LEN] = { 0 };
	int r = ctrl(client, 0x21, 1, 0, 0, zeros, DFU_FILE_SUFFIX_LEN);
	fprintf(stderr, "suffix r=%d\n", r);
	r = ctrl(client, 0x21, 1, 0, 0, NULL, 0);
	fprintf(stderr, "zlp r=%d\n", r);

	/* gaster dfu_set_state_wait_reset: 3x GETSTATUS walking
	 * MANIFEST_SYNC(6) -> MANIFEST(7) -> MANIFEST_WAIT_RESET(8) */
	for (int i = 0; i < 3; i++) {
		unsigned char st[6] = { 0 };
		int sr = get_status(client, st);
		fprintf(stderr, "status[%d] r=%d status=%u state=%u\n",
		        i, sr, st[0], st[4]);
	}

	/* Bus reset -> uploaded image boots */
	r = irecv_reset(client);
	fprintf(stderr, "irecv_reset r=%d\n", r);

	irecv_close(client);
	return (r < 0) ? 1 : 0;
}
