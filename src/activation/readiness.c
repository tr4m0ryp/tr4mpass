/*
 * readiness.c -- Bounded post-reboot activation service readiness probe.
 *
 * After a reboot, mobileactivationd may need several seconds before it
 * accepts service-start and state-query requests.  This module retries
 * transient failures a bounded number of times, treating permanent
 * protocol errors as immediate failures.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libimobiledevice/mobileactivation.h>
#include <plist/plist.h>

#include "activation/activation.h"
#include "device/device.h"
#include "util/log.h"

#define LOG_TAG "[readiness]"

/* Delay between transient attempts, in microseconds.  Zero under UNIT_TEST
 * so tests run fast; 2,000,000 (2s) in production builds. */
#ifdef UNIT_TEST
#define READY_DELAY_US 0
#else
#define READY_DELAY_US 2000000L
#endif

/* Permanent errors that must not be retried. */
static int is_permanent_error(int err)
{
    return err == MOBILEACTIVATION_E_INVALID_ARG ||
           err == MOBILEACTIVATION_E_PLIST_ERROR ||
           err == MOBILEACTIVATION_E_UNKNOWN_REQUEST;
}

int activation_wait_ready(device_info_t *dev, unsigned max_attempts)
{
    mobileactivation_client_t ma = NULL;
    mobileactivation_error_t err;
    int last_err = MOBILEACTIVATION_E_SUCCESS;
    unsigned attempt;
    plist_t state = NULL;

    if (!dev || !dev->handle || max_attempts == 0) {
        log_error("%s Invalid arguments (dev=%p handle=%p max_attempts=%u)",
                  LOG_TAG, (void *)dev,
                  dev ? (void *)dev->handle : NULL, max_attempts);
        return -1;
    }

    for (attempt = 1; attempt <= max_attempts; attempt++) {
        ma = NULL;
        state = NULL;

        /* Step 1: start the mobileactivation service. */
        err = mobileactivation_client_start_service(dev->handle, &ma,
                                                    "tr4mpass");
        if (err != MOBILEACTIVATION_E_SUCCESS) {
            last_err = (int)err;
            if (is_permanent_error(last_err)) {
                log_error("%s Permanent startup error %d on attempt %u/%u",
                          LOG_TAG, last_err, attempt, max_attempts);
                return -1;
            }
            log_warn("%s Startup failed (error %d), attempt %u/%u",
                     LOG_TAG, last_err, attempt, max_attempts);
            if (attempt < max_attempts)
                usleep(READY_DELAY_US);
            continue;
        }

        /* Step 2: startup succeeded; query the activation state. */
        err = mobileactivation_get_activation_state(ma, &state);
        if (err != MOBILEACTIVATION_E_SUCCESS) {
            last_err = (int)err;
            mobileactivation_client_free(ma);
            ma = NULL;
            if (is_permanent_error(last_err)) {
                log_error("%s Permanent state-query error %d on attempt %u/%u",
                          LOG_TAG, last_err, attempt, max_attempts);
                return -1;
            }
            log_warn("%s State query failed (error %d), attempt %u/%u",
                     LOG_TAG, last_err, attempt, max_attempts);
            if (state)
                plist_free(state);
            state = NULL;
            if (attempt < max_attempts)
                usleep(READY_DELAY_US);
            continue;
        }

        /* Success: state query returned. */
        if (state)
            plist_free(state);
        mobileactivation_client_free(ma);
        log_info("%s Device activation service ready after %u attempt(s)",
                 LOG_TAG, attempt);
        return 0;
    }

    /* Exhausted all attempts. */
    log_error("%s Activation service not ready after %u attempt(s) "
              "(last error %d)", LOG_TAG, max_attempts, last_err);
    return -1;
}
