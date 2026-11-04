/* stage.h -- Common stage types and lifecycle API */

#ifndef STAGE_H
#define STAGE_H

#include <stddef.h>
#include <stdint.h>

#include "device/device.h"
#include "device/chip_db.h"

/* Stage phase constants */
#define DFU_PHASE_INIT   0
#define DFU_PHASE_RESET  1
#define DFU_PHASE_SETUP  2
#define DFU_PHASE_SPRAY  3
#define DFU_PHASE_PATCH  4
#define DFU_PHASE_DONE   5

/*
 * dfu_ctx_t -- shared context for any DFU stage.
 *
 * Holds the device, chip info, current stage phase, and the assembled
 * payload buffer (redirect chain + stage4 code concatenated).
 */
typedef struct {
    device_info_t       *dev;
    const chip_info_t   *chip;
    int                  phase;       /* DFU_PHASE_* */
    uint8_t             *payload_buf;
    size_t               payload_len;
    int                  skip_clrstatus; /* 1 = DFU already in dfuIDLE, skip CLR_STATUS in stage 3 */
} dfu_ctx_t;

/*
 * dfu_init -- Populate ctx from device info. Looks up chip_info via
 * cpid, verifies checkm8 compatibility condition, allocates payload buffer.
 * Returns 0 on success, -1 on failure.
 */
int dfu_init(dfu_ctx_t *ctx, device_info_t *dev);

/*
 * dfu_deliver -- Execute the stage stages (reset, spray, setup, patch).
 * The specific stage is selected based on chip type (currently only checkm8).
 * Returns 0 on success, -1 on failure.
 */
int dfu_deliver(dfu_ctx_t *ctx);

/*
 * dfu_cleanup -- Free payload buffer and zero the context.
 */
void dfu_cleanup(dfu_ctx_t *ctx);

#endif /* STAGE_H */
