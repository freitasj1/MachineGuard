/**
 * @file dsp.c
 * @brief DSP task implementation.
 */

#include "dsp.h"

#include "app_context.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "portmacro.h"
#include <stdint.h>

#include <float.h>
#include <math.h>

/* ============================================================================
* Private constants and macros  
* ========================================================================== */

static const char *TAG = "dsp";
typedef struct
{
    float mean;
    float rms;
    float stddev;
    float minimum;
    float maximum;
    float peak_to_peak;

} dsp_time_stats_t;

typedef struct
{
    float time_signal[ACCEL_BLOCK_SIZE];

    dsp_time_stats_t time_stats;

} dsp_context_t;

static dsp_context_t s_dsp;

/* ============================================================================
* Private variables
* ========================================================================== */


 
/* ============================================================================ 
* Private function prototypes
* ========================================================================== */
 
static float remove_dc_offset(const accel_block_t *input,
                               float *output);
static void calculate_time_stats(const float *signal, 
    dsp_time_stats_t *stats);

/* ============================================================================
 * Public function implementations
 * ========================================================================== */

void task_dsp(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    if (ctx == NULL || ctx->queue_accel_block == NULL ||
        ctx->queue_dsp_result == NULL) {
        ESP_LOGE(TAG, "invalid DSP context");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "task started");
    while (true) {
        accel_block_t block;
        if (xQueueReceive(ctx->queue_accel_block, &block,
             portMAX_DELAY) == pdTRUE) {
            /* TODO: process block and overwrite queue_dsp_result. */
            s_dsp.time_stats.mean = 
                remove_dc_offset(&block, s_dsp.time_signal);
        
            calculate_time_stats(s_dsp.time_signal,
                     &s_dsp.time_stats);

            ESP_LOGI(TAG,
                "Mean: %.2f | RMS: %.2f | Min: %.2f | Max: %.2f | PkPk: %.2f",
                s_dsp.time_stats.mean,
                s_dsp.time_stats.rms,
                s_dsp.time_stats.minimum,
                s_dsp.time_stats.maximum,
                s_dsp.time_stats.peak_to_peak);
        }
    }
}

/* ============================================================================
 * Private function implementations
 * ========================================================================== */

 static float remove_dc_offset(const accel_block_t *input, float *output)
{
    float mean = 0.0f;

    /* Calculate block mean. */
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        mean += (float)input->samples[i];
    }
    mean /= (float)ACCEL_BLOCK_SIZE;

    /* Convert to float and remove DC offset. */
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        output[i] = (float)input->samples[i] - mean;
    }

    return mean;
}

static void calculate_time_stats(const float *signal,
                                 dsp_time_stats_t *stats)
{
    float sum_squares = 0.0f;

    stats->minimum = FLT_MAX;
    stats->maximum = -FLT_MAX;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        const float sample = signal[i];

        sum_squares += sample * sample;

        if (sample < stats->minimum) {
            stats->minimum = sample;
        }

        if (sample > stats->maximum) {
            stats->maximum = sample;
        }
    }

    stats->rms = sqrtf(sum_squares / (float)ACCEL_BLOCK_SIZE);

    stats->peak_to_peak = stats->maximum - stats->minimum;

    /* Calculated in a future step. */
    stats->stddev = 0.0f;
}