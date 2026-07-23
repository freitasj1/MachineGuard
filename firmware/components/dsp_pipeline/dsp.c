/**
 * @file dsp.c
 * @brief DSP task implementation.
 */

#include "dsp.h"

#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "dsp";

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
        if (xQueueReceive(ctx->queue_accel_block, &block, portMAX_DELAY) == pdTRUE) {
            /* TODO: process block and overwrite queue_dsp_result. */
        }
    }
}
