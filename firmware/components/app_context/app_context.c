/**
 * @file app_context.c
 * @brief Shared application context implementation.
 */

#include "app_context.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/idf_additions.h"

/* ============================================================================
 * Private constants and macros
 * ========================================================================== */

static const char *TAG = "app_context";

/* ============================================================================
 * Private function prototypes
 * ========================================================================== */

static void delete_resources(app_context_t *ctx);

/* ============================================================================
 * Public function implementations
 * ========================================================================== */

esp_err_t app_context_init(app_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->queue_accel_block_to_dsp =
        xQueueCreate(1, sizeof(accel_block_t));

    if (ctx->queue_accel_block_to_dsp == NULL) {
        goto allocation_failed;
    }

    ctx->queue_dsp_to_system =
        xQueueCreate(1, sizeof(dsp_result_t));

    if (ctx->queue_dsp_to_system == NULL) {
        goto allocation_failed;
    }

    ctx->mutex_spi2 = xSemaphoreCreateMutex();

    if (ctx->mutex_spi2 == NULL) {
        goto allocation_failed;
    }

    ESP_LOGI(TAG, "context initialized");

    return ESP_OK;

allocation_failed:

    delete_resources(ctx);

    memset(ctx, 0, sizeof(*ctx));

    return ESP_ERR_NO_MEM;
}

/* ============================================================================
 * Private function implementations
 * ========================================================================== */

static void delete_resources(app_context_t *ctx)
{
    if (ctx->queue_accel_block_to_dsp != NULL) {
        vQueueDelete(ctx->queue_accel_block_to_dsp);
        ctx->queue_accel_block_to_dsp = NULL;
    }

    if (ctx->queue_dsp_to_system != NULL) {
        vQueueDelete(ctx->queue_dsp_to_system);
        ctx->queue_dsp_to_system = NULL;
    }

    if (ctx->mutex_spi2 != NULL) {
        vSemaphoreDelete(ctx->mutex_spi2);
        ctx->mutex_spi2 = NULL;
    }
}