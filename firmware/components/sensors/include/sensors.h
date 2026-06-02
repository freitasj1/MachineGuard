/**
 * @file sensors.h
 * @brief Interface da task de sensores
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de coleta de sensores
 * @param arg Ponteiro para app_context_t
 */
void task_sensors(void *arg);

#ifdef __cplusplus
}
#endif