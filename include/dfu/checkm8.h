/* checkm8.h -- checkm8 DFU stage for A5-A11 BootROM */

#ifndef CHECKM8_H
#define CHECKM8_H

#include "device/device.h"
#include "dfu/dfu_handler.h"

/*
 * checkm8_run -- Top-level entry point. Sets up context, runs all four
 * stages (reset, spray, setup, patch), verifies processed state, cleans up.
 * Returns 0 on success, -1 on failure.
 */
int checkm8_run(device_info_t *dev);

/*
 * Individual stage stages. Each advances ctx->phase on success.
 * All return 0 on success, -1 on failure.
 */

/*
 * Stage 1: Reset DFU state machine to a known clean state.
 * Sends DFU_DNLOAD with suffix-length zeroes, polls through manifest
 * states, then resets to dfuIDLE.
 */
int checkm8_stage_reset(dfu_ctx_t *ctx);

/*
 * Stage 2: Setup -- trigger use-after-free. Sends an async DFU_DNLOAD
 * with DFU_MAX_TRANSFER_SZ bytes, then overwrite padding to provoke
 * a stall that frees the io_buffer while the pointer remains live.
 */
int checkm8_stage_setup(dfu_ctx_t *ctx);

/*
 * Stage 3: Heap spray. Two paths based on chip config:
 *   Path A (large_leak > 0): stall-based GET_DESCRIPTOR spray.
 *   Path B (large_leak == 0): send/stall cycles to create heap holes.
 */
int checkm8_stage_spray(dfu_ctx_t *ctx);

/*
 * Stage 4: Overwrite freed callback with controlled data, then send
 * the payload (chip-specific header + stage4 code) in DFU_MAX_TRANSFER_SZ
 * chunks. After execution the serial string contains "PWND:[checkm8]".
 */
int checkm8_stage_patch(dfu_ctx_t *ctx);

/*
 * checkm8_verify -- Re-read the USB serial string descriptor and
 * check whether it contains "PWND:".
 * Returns 1 if processed, 0 if not processed, -1 on read error.
 */
int checkm8_verify(device_info_t *dev);

#endif /* CHECKM8_H */
