#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "components/app_context/app_context.h"

static const char *TAG = "main";

// ─── Protótipos das tasks ─────────────────────────────────────────────────────
// Cada task será implementada no seu componente próprio.
// Declaradas aqui como extern até os componentes existirem.
//
// TODO: remover os extern e incluir os headers quando cada componente
//       for criado (ex: #include "dsp_pipeline.h")

extern void task_dsp(void *pv_ctx);   // Core 0 — componente dsp_pipeline
extern void task_pcnt(void *pv_ctx);  // Core 1 — componente rpm_counter
extern void task_hmi(void *pv_ctx);   // Core 1 — componente hmi
extern void task_dac(void *pv_ctx);   // Core 1 — componente dac_output
extern void task_sd(void *pv_ctx);    // Core 1 — componente storage

// ─── Contexto global do sistema ──────────────────────────────────────────────
// Único ponto de dados compartilhados — passado por ponteiro para cada task.
static app_context_t ctx;

void app_main(void)
{
    ESP_LOGI(TAG, "MachineGuard iniciando...");

    // Inicializa filas, mutex e atomic antes de qualquer task
    ESP_ERROR_CHECK(app_context_init(&ctx));

    // ── Core 0 ───────────────────────────────────────────────────────────────
    xTaskCreatePinnedToCore(
        task_dsp,           // função da task
        "dsp",              // nome (debug / uxTaskGetStackHighWaterMark)
        8192,               // stack em words (32 KB) — buffers FFT + SIMD
        &ctx,               // parâmetro: ponteiro para o contexto
        24,                 // prioridade máxima — nunca preemptada
        NULL,               // handle (não necessário por ora)
        0                   // Core 0
    );

    // ── Core 1 ───────────────────────────────────────────────────────────────
    xTaskCreatePinnedToCore(
        task_pcnt,
        "pcnt",
        1024,               // stack mínimo — só aritmética float + PCNT
        &ctx,
        12,                 // maior prioridade do Core 1
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        task_hmi,
        "hmi",
        4096,               // LovyanGFX C++ + lógica de telas
        &ctx,
        10,
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        task_dac,
        "dac",
        2048,               // FIR + I2C MCP4725
        &ctx,
        9,
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        task_sd,
        "sd",
        2048,               // FatFS + buffer CSV
        &ctx,
        8,                  // menor prioridade — log não é tempo-crítico
        NULL,
        1
    );

    ESP_LOGI(TAG, "Tasks criadas. FreeRTOS assumindo controle.");

    // app_main retorna aqui — FreeRTOS scheduler já está rodando desde o boot
    // no ESP-IDF. Não é necessário vTaskStartScheduler().
}