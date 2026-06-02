/**
 * @file hmi.h
 * @brief Interface da task HMI
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de interface humano-máquina
 * @param arg Ponteiro para app_context_t
 */
void task_hmi(void *arg);

#ifdef __cplusplus
}
#endif