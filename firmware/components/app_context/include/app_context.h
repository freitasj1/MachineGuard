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

/* ============================================================================
 * Public constants and macros
 * ========================================================================== */

enum {
    ACCEL_BLOCK_SIZE = 2048,
    DSP_FFT_MAGNITUDE_SIZE = ACCEL_BLOCK_SIZE / 2,

    /** Native FFT bins transported to the HMI (5 Hz to 250 Hz range). */
    HMI_FFT_FIRST_BIN = 2,
    HMI_FFT_LAST_BIN = 76,
    HMI_FFT_POINT_COUNT = HMI_FFT_LAST_BIN - HMI_FFT_FIRST_BIN + 1
};

/* ============================================================================
 * Public types
 * ========================================================================== */

/**
 * @brief Machine operating state.
 */
typedef enum
{
    SYSTEM_STATE_INIT = 0,
    SYSTEM_STATE_WARMUP,
    SYSTEM_STATE_HEALTHY,
    SYSTEM_STATE_ALARM

} system_state_t;

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

    bool peak_valid;

} dsp_result_t;

/**
 * @brief Temperature result produced by the sensors component.
 */
typedef struct
{
    float temperature_c;
    bool temperature_valid;

} sensor_result_t;

/**
 * @brief Machine state produced by the system task.
 */
typedef struct
{
    system_state_t state;

} system_state_output_t;

/**
 * @brief Machine measurements produced by the system task.
 */
typedef struct
{
    float rms;
    float kurtosis;
    float crest_factor;

    float bin_1xrpm_amplitude;
    float frequency_hz;
    float rpm;

    float temperature_c;
    bool temperature_valid;

} system_features_t;

/**
 * @brief Diagnostic information produced by the system task.
 *
 * Contains the individual feature evaluations used by the 2-of-3
 * abnormality decision.
 */
typedef struct
{
    float rms_zscore;
    float kurtosis_zscore;
    float bin_1xrpm_zscore;

    bool rms_abnormal;
    bool kurtosis_abnormal;
    bool bin_1xrpm_abnormal;

} system_diagnostics_t;

/**
 * @brief Warm-up information produced by the system task.
 */
typedef struct
{
    uint16_t evaluations;
    uint16_t required;
    uint16_t bin_1xrpm_valid;

} system_warmup_t;

/**
 * @brief Commands sent from the HMI to the system task.
 */
typedef enum
{
    SYSTEM_COMMAND_RESET_WARMUP

} system_command_t;

/**
 * @brief Waveform data produced by the system task for DAC output.
 *
 * The waveform is expressed in g and contains one complete DSP block.
 */
typedef struct
{
    float waveform[ACCEL_BLOCK_SIZE];

} dac_waveform_t;

/**
 * @brief Telemetry data produced by the system task.
 *
 * Contains the machine information intended for MQTT transmission.
 */
typedef struct
{
    system_state_t state;

    float rms;
    float kurtosis;
    float crest_factor;

    float bin_1xrpm_amplitude;
    float frequency_hz;
    float rpm;

    float temperature_c;
    bool temperature_valid;

    float rms_zscore;
    float kurtosis_zscore;
    float bin_1xrpm_zscore;

    bool rms_abnormal;
    bool kurtosis_abnormal;
    bool bin_1xrpm_abnormal;

} telemetry_data_t;

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
 * @brief Data produced by the system task for the HMI.
 *
 * Contains only information required by the HMI.
 */
typedef struct
{
    system_state_output_t state;
    system_features_t features;
    system_diagnostics_t diagnostics;
    system_warmup_t warmup;

    /** Magnitudes of native FFT bins 2 through 76, inclusive. */
    float fft_magnitude[HMI_FFT_POINT_COUNT];

} hmi_data_t;

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

    /** Latest system data consumed by the HMI. */
    QueueHandle_t queue_system_to_hmi;

    /** Commands sent from the HMI to the system task. */
    QueueHandle_t queue_hmi_to_system;

    /** Latest waveform produced for DAC output. */
    QueueHandle_t queue_system_to_dac;

    /** Latest telemetry data produced by the system task. */
    QueueHandle_t queue_system_to_telemetry;

    /** Latest sensor result consumed by the system task. */
    QueueHandle_t queue_sensors_to_system;

    /** SPI2 mutex shared by components using SPI2. */
    SemaphoreHandle_t mutex_spi2;

} app_context_t;

/* ============================================================================
 * Public function prototypes
 * ========================================================================== */

esp_err_t app_context_init(app_context_t *ctx);

#ifdef __cplusplus
}
#endif
