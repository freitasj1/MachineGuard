/**
 * @file dsp.c
 * @brief Implementação da task de processamento DSP
 */

#include "dsp.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

static const char *TAG = "dsp";

void task_dsp(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_dsp iniciada");
    while (1) {
        // TODO: ler amostras do acelerômetro e gerar resultados DSP
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}