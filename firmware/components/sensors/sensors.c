/**
 * @file sensors.c
 * @brief Implementação da task de coleta de sensores
 */

#include "sensors.h"
#include "app_context.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensors";

void task_sensors(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    (void)ctx;  // Suprime warning de variável não utilizada
    
    ESP_LOGI(TAG, "task_sensors iniciada no Core 1");
    ESP_LOGI(TAG, "Coleta de dados: acelerômetro + RPM");
    
    while (1) {
        // TODO: Implementar leitura do acelerômetro via SPI2
        // TODO: Implementar leitura do RPM via PCNT
        // TODO: Enviar dados para queue_dsp_result quando prontos
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}