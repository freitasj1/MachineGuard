/**
 * @file hmi.c
 * @brief Implementação da task HMI
 */

#include "hmi.h"
#include "app_context.h"
#include "esp_log.h"

static const char *TAG = "hmi";

void task_hmi(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    (void)ctx;  // Suprime warning
    ESP_LOGI(TAG, "task_hmi iniciada no Core 1");
    
    while (1) {
        // TODO: Implementar HMI:
        // 1. Ler dados de queue_dsp_result
        // 2. Atualizar display com valores
        // 3. Processar entrada de botões
        // 4. Controlar LEDs de status
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}