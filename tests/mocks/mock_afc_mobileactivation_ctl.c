/*
 * mock_afc_mobileactivation_ctl.c -- Staging + reset for the AFC /
 * mobileactivation mock.  Kept separate from _sym.c so each TU stays
 * under the 300-line project cap.
 */

#include "mock_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared with the sym file via extern declarations in the header below. */
int  g_ma_activate_error;
int  g_ma_create_info_error;
char g_ma_state[64] = "Activated";

/* Independent staged failure pairs for startup and state queries. */
int g_ma_start_service_error;
int g_ma_start_service_failures_left;
int g_ma_state_query_error;
int g_ma_state_query_failures_left;

void mock_mobileactivation_set_activate_error(int error_code)
{
    g_ma_activate_error = error_code;
}

void mock_mobileactivation_set_create_info_error(int error_code)
{
    g_ma_create_info_error = error_code;
}

void mock_mobileactivation_set_state(const char *state)
{
    if (!state)
        snprintf(g_ma_state, sizeof(g_ma_state), "%s", "Activated");
    else
        snprintf(g_ma_state, sizeof(g_ma_state), "%s", state);
}

void mock_mobileactivation_set_start_service_failures(int error_code, int failure_count)
{
    g_ma_start_service_error       = error_code;
    g_ma_start_service_failures_left = failure_count;
}

void mock_mobileactivation_set_state_query_failures(int error_code, int failure_count)
{
    g_ma_state_query_error       = error_code;
    g_ma_state_query_failures_left = failure_count;
}

void mock_afc_mobileactivation_reset(void)
{
    g_ma_activate_error    = 0;
    g_ma_create_info_error = 0;
    snprintf(g_ma_state, sizeof(g_ma_state), "%s", "Activated");
    g_ma_start_service_error       = 0;
    g_ma_start_service_failures_left = 0;
    g_ma_state_query_error       = 0;
    g_ma_state_query_failures_left = 0;
}
