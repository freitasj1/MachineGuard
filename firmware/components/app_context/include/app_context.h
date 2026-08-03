/**
 * @file app_context.h
 * @brief Shared application context.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kurtosis;
    float rms;
    float crest_factor;
    float bin_1xrpm_amplitude;
    float zscore_kurtosis;
    float zscore_rms;
    float zscore_bin;
    bool alert_active;
    bool warmup_active;
} dsp_result_t;

/** One selected-axis accelerometer sample. */
typedef int16_t accel_sample_t;

/** Samples delivered in each acquisition-to-DSP message. */
enum { ACCEL_BLOCK_SIZE = 2048 };

/**
 * Queue payload from accelerometer to DSP.
 * The queue copies this block; acquisition ping-pong buffers stay private.
 */
typedef struct {
    accel_sample_t samples[ACCEL_BLOCK_SIZE];
} accel_block_t;

/**
 * Shared synchronization and inter-task communication resources.
 * This context never owns a component's private acquisition buffers.
 */
typedef struct {
    /** Latest DSP result, written by DSP and read by Core 1 consumers. */
    QueueHandle_t queue_dsp_result;

    /** Latest selected-axis acquisition block, written by accelerometer. */
    QueueHandle_t queue_accel_block;

    /** SPI2 mutex shared by accelerometer and storage. */
    SemaphoreHandle_t mutex_spi2;
} app_context_t;

esp_err_t app_context_init(app_context_t *ctx);

#ifdef __cplusplus
}
#endif
