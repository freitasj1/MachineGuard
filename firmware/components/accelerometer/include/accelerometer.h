/**
 * @file accelerometer.h
 * @brief Interface do driver de acelerômetro
 */

#pragma once

#include "app_context.h"
#include <stdint.h>
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACCEL_MG_PER_LSB 0.061f

esp_err_t accel_init(app_context_t *ctx);
void task_accel(void *arg);



#ifdef __cplusplus
}
#endif
