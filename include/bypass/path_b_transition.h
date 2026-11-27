/*
 * path_b_transition.h -- Transport-aware DFU-to-Recovery transition.
 */

#ifndef PATH_B_TRANSITION_H
#define PATH_B_TRANSITION_H

#include "device/device.h"

/*
 * Transition a DFU-mode device to Recovery without crossing transport
 * boundaries.  Classic DFU uses the existing raw DFU abort request; KIS
 * requires the explicit experimental opt-in and issues one reboot request.
 *
 * Recovery is accepted only when libirecovery reports mode 0x1280..0x1283.
 * Returns 0 on confirmed Recovery and -1 on refusal, request failure, or
 * timeout.
 */
int path_b_transition_to_recovery(device_info_t *dev,
                                  unsigned int timeout_secs,
                                  unsigned int poll_secs);

#endif /* PATH_B_TRANSITION_H */
