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

esp_err_t accel_init(SemaphoreHandle_t spi2_mutex);
esp_err_t accel_read_raw(accel_sample_t *out);

#ifdef __cplusplus
}
#endif
