/**
 * @file main.c
 * @brief Inicialização do sistema MachineGuard
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
 * @brief Ponto de entrada da aplicação
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