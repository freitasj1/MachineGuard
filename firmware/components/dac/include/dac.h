/**
 * @file dac.h
 * @brief Interface da task DAC
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de DAC
 * @param arg Ponteiro para app_context_t
 */
void task_dac(void *arg);

#ifdef __cplusplus
}
#endif