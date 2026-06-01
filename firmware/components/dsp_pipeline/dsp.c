/**
 * @file dsp.c
 * @brief Implementação da task de processamento DSP
 */

#include "dsp.h"
#include "app_context.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include <math.h>
#include <stddef.h>

/* Forward-declare sensitivity macro if not available from accelerometer header */
#ifndef ACCEL_MG_PER_LSB
#define ACCEL_MG_PER_LSB 0.061f
#endif

static const char *TAG = "dsp";

void task_dsp(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    ESP_LOGI(TAG, "task_dsp iniciada no Core 0");
    
    while (1) {
        /* Consome as amostras brutas do acelerômetro e converte para float (g)
         * A sensibilidade é definida por ACCEL_MG_PER_LSB (mg/LSB)
         */
        size_t n = 0;
        if (xSemaphoreTake(ctx->mutex_accel_buffer, pdMS_TO_TICKS(10)) == pdTRUE) {
            n = ctx->accel_input.count;
            if (n > 0) {
                size_t start = (ctx->accel_input.head + ACCEL_WINDOW_SIZE - n) % ACCEL_WINDOW_SIZE;
                float sumsq = 0.0f;
                for (size_t i = 0; i < n; ++i) {
                    accel_sample_t s = ctx->accel_input.samples[(start + i) % ACCEL_WINDOW_SIZE];
                    /* converte int16 -> g */
                    float gx = (s.x * ACCEL_MG_PER_LSB) / 1000.0f;
                    float gy = (s.y * ACCEL_MG_PER_LSB) / 1000.0f;
                    float gz = (s.z * ACCEL_MG_PER_LSB) / 1000.0f;
                    float mag = sqrtf(gx*gx + gy*gy + gz*gz);
                    sumsq += mag * mag;
                }
                float rms = sqrtf(sumsq / (float)n);

                dsp_result_t res = {0};
                res.rms = rms;
                /* Enviar resultado para queue (overwrite) */
                xQueueOverwrite(ctx->queue_dsp_result, &res);

                /* Marca buffer como consumido */
                ctx->accel_input.count = 0;
            }
            xSemaphoreGive(ctx->mutex_accel_buffer);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}