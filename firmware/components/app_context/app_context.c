/**
 * @file app_context.c
 * @brief Shared application context implementation.
 */

#include "app_context.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "app_context";
esp_err_t app_context_init(app_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->queue_accel_block_to_dsp   = xQueueCreate(1, sizeof(accel_block_t));

    ctx->queue_dsp_to_hmi    = xQueueCreate(1, sizeof(dsp_result_t));
    ctx->queue_dsp_to_storage= xQueueCreate(1, sizeof(dsp_result_t));
    ctx->queue_dsp_to_dac    = xQueueCreate(1, sizeof(dsp_result_t));

    ctx->mutex_spi2 = xSemaphoreCreateMutex();

    if (ctx->queue_accel_block_to_dsp == NULL ||
        ctx->queue_dsp_to_hmi == NULL ||
        ctx->queue_dsp_to_storage == NULL ||
        ctx->queue_dsp_to_dac == NULL ||
        ctx->mutex_spi2 == NULL) {

        if (ctx->queue_accel_block_to_dsp != NULL) {
            vQueueDelete(ctx->queue_accel_block_to_dsp);
        }

        if (ctx->queue_dsp_to_hmi != NULL) {
            vQueueDelete(ctx->queue_dsp_to_hmi);
        }

        if (ctx->queue_dsp_to_storage != NULL) {
            vQueueDelete(ctx->queue_dsp_to_storage);
        }

        if (ctx->queue_dsp_to_dac != NULL) {
            vQueueDelete(ctx->queue_dsp_to_dac);
        }

        if (ctx->mutex_spi2 != NULL) {
            vSemaphoreDelete(ctx->mutex_spi2);
        }

        memset(ctx, 0, sizeof(*ctx));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "context initialized");

    return ESP_OK;
}
