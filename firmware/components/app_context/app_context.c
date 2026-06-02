/**
 * @file app_context.c
 * @brief Implementação do contexto compartilhado do sistema
 */

#include "app_context.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <stdatomic.h>

static const char *TAG = "app_context";

esp_err_t app_context_init(app_context_t *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "app_context_init chamado");
    // TODO: criar queue_dsp_result e mutexes

    // TODO: inicializar demais dados do contexto
    return ESP_OK;
}