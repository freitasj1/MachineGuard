/**
 * @file accelerometer.h
 * @brief Accelerometer acquisition task interface.
 */

#pragma once

#include "app_context.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t accel_init(app_context_t *ctx);
void task_accel(void *arg);

#ifdef __cplusplus
}
#endif
