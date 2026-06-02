/**
 * @file dsp.h
 * @brief Interface da task DSP
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de processamento DSP
 * @param arg Ponteiro para app_context_t
 */
void task_dsp(void *arg);

#ifdef __cplusplus
}
#endif