/**
 * @file app_context.h
 * @brief Shared application context.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Public constants and macros
 * ========================================================================== */

enum {
    ACCEL_BLOCK_SIZE = 2048,
    DSP_FFT_MAGNITUDE_SIZE = ACCEL_BLOCK_SIZE / 2
};

/* ============================================================================
 * Public types
 * ========================================================================== */

/**
 * @brief Result produced by the DSP pipeline for the system task.
 *
 * The DSP result contains only signal-analysis data.
 * Machine state, warm-up, Z-score, thresholds and alarms belong to system.
 */
typedef struct
{
    /* Time-domain indicators. */
    float rms;
    float kurtosis;
    float crest_factor;

    /* 1xRPM spectral component. */
    float bin_1xrpm_amplitude;
    float frequency_hz;
    float rpm;

    /* FFT magnitude spectrum for HMI visualization. */
    float magnitude[DSP_FFT_MAGNITUDE_SIZE];

    /* Time-domain signal for DAC output, expressed in g. */
    float waveform[ACCEL_BLOCK_SIZE];

} dsp_result_t;

/**
 * @brief One selected-axis accelerometer sample.
 */
typedef int16_t accel_sample_t;

/**
 * @brief Samples delivered in each acquisition-to-DSP message.
 *
 * The queue copies this block; acquisition ping-pong buffers remain private
 * to the accelerometer component.
 */
typedef struct
{
    accel_sample_t samples[ACCEL_BLOCK_SIZE];

} accel_block_t;

/**
 * @brief Shared synchronization and inter-task communication resources.
 *
 * This context owns communication primitives, but never owns component
 * private acquisition or processing buffers.
 */
typedef struct
{
    /** Latest selected-axis acquisition block. */
    QueueHandle_t queue_accel_block_to_dsp;

    /** Latest DSP result consumed by the system task. */
    QueueHandle_t queue_dsp_to_system;

    /** SPI2 mutex shared by accelerometer and storage. */
    SemaphoreHandle_t mutex_spi2;

} app_context_t;

/* ============================================================================
 * Public function prototypes
 * ========================================================================== */

esp_err_t app_context_init(app_context_t *ctx);

#ifdef __cplusplus
}
#endif