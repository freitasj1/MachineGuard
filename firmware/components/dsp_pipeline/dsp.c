/**
 * @file dsp.c
 * @brief Implementação da task de processamento DSP
 */

#include "dsp.h"
#include "app_context.h"
#include "esp_log.h"

static const char *TAG = "dsp";

void task_dsp(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    ESP_LOGI(TAG, "task_dsp iniciada no Core 0");
    
    while (1) {
        // TODO: Implementar pipeline DSP:
        // 1. Ler dados de sensores da queue
        // 2. Aplicar FFT e análise espectral
        // 3. Calcular curtose, RMS, crest factor
        // 4. Comparar com thresholds e z-scores
        // 5. Preencher dsp_result_t e enviar via queue
        (void)ctx;  // Suprime warning
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}