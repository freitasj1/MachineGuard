/**
 * @file storage.c
 * @brief Implementação da task de armazenamento
 */

#include "storage.h"
#include "app_context.h"
#include "esp_log.h"

static const char *TAG = "storage";

void task_sd(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    (void)ctx;  // Suprime warning
    ESP_LOGI(TAG, "task_sd iniciada no Core 1");
    
    while (1) {
        // TODO: Implementar storage:
        // 1. Ler dados de queue_dsp_result
        // 2. Adquirir mutex_spi2
        // 3. Escrever em arquivo CSV/binário no SD card
        // 4. Liberar mutex_spi2
        // 5. Gerenciar tamanho de arquivo e rotação
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}