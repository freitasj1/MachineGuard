/**
 * @file main.c
 * @brief Arquivo principal - Inicialização do sistema MachineGuard
 * @details Configura o contexto compartilhado e cria todas as tasks FreeRTOS
 *          pinadas a cores específicos com prioridades definidas.
 */

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "app_context.h"
#include "dac.h"
#include "dsp.h"
#include "hmi.h"
#include "sensors.h"
#include "storage.h"

static const char *TAG = "main";  /**< Tag para logs ESP-IDF */
static app_context_t ctx;           /**< Contexto compartilhado do sistema */

/**
 * @brief Função principal da aplicação
 * @details Inicializa o contexto compartilhado e cria todas as tasks:
 *          - Core 0: task_dsp (DSP, prioridade 24, 8KB stack)
 *          - Core 1: task_sensors (Sensores, prioridade 12, 4KB stack)
 *          - Core 1: task_hmi (Interface, prioridade 10, 4KB stack)
 *          - Core 1: task_dac (Áudio, prioridade 9, 4KB stack)
 *          - Core 1: task_sd (Armazenamento, prioridade 8, 4KB stack)
 */
void app_main(void)
{
    ESP_LOGI(TAG, "MachineGuard iniciando...");


     ESP_ERROR_CHECK(app_context_init(&ctx));

    // função da task, nome de debug, stack em bytes, parâmetro(ponteiro para o contexto), prioridade, handle (NULL), core
    xTaskCreatePinnedToCore(task_dsp,     "dsp",     8192, &ctx, 24, NULL, 0);
    xTaskCreatePinnedToCore(task_sensors, "sensors", 4096, &ctx, 12, NULL, 1);
    xTaskCreatePinnedToCore(task_hmi,  "hmi",  4096, &ctx, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_dac,  "dac",  4096, &ctx,  9, NULL, 1);
    xTaskCreatePinnedToCore(task_sd,   "sd",   4096, &ctx,  8, NULL, 1);

}