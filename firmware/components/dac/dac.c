/**
 * @file dac.c
 * @brief Implementação da task DAC
 */

#include "dac.h"
#include "app_context.h"
#include "esp_log.h"

static const char *TAG = "dac";

void task_dac(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    (void)ctx;  // Suprime warning
    ESP_LOGI(TAG, "task_dac iniciada no Core 1");
    
    while (1) {
        // TODO: Implementar DAC:
        // 1. Ler dados de queue_dsp_result
        // 2. Verificar se alert_active está ativo
        // 3. Gerar sinal sonoro via DAC se necessário
        // 4. Controlar volume/frequência de alerta
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}