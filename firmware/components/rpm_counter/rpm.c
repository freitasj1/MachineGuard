#include "app_context.h"
#include "esp_log.h"

static const char *TAG = "app_context";

esp_err_t app_context_init(app_context_t *ctx)
{
    // Fila DSP: comprimento 1, overwrite — sempre o resultado mais recente
    ctx->queue_dsp_result = xQueueCreate(1, sizeof(dsp_result_t));
    if (ctx->queue_dsp_result == NULL) {
        ESP_LOGE(TAG, "Falha ao criar queue_dsp_result");
        return ESP_ERR_NO_MEM;
    }

    // Mutex SPI2 com herança de prioridade — previne priority inversion
    // entre task_sd (prio 8) e task_dsp (prio 24)
    ctx->mutex_spi2 = xSemaphoreCreateMutex();
    if (ctx->mutex_spi2 == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex_spi2");
        return ESP_ERR_NO_MEM;
    }

    // RPM começa em zero — task_pcnt atualiza após primeira leitura PCNT
    atomic_init(&ctx->current_rpm, 0.0f);

    ESP_LOGI(TAG, "app_context inicializado");
    return ESP_OK;
}