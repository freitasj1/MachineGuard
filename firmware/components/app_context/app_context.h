#pragma once

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Resultado de um ciclo DSP completo ──────────────────────────────────────
// Produzido pelo Core 0 (task_dsp) e consumido pelo Core 1 (task_hmi, task_sd,
// task_dac) via queue_dsp_result.
// Campos marcados com TODO serão preenchidos quando os componentes existirem.
typedef struct {
    float    kurtosis;
    float    rms;
    float    crest_factor;
    float    bin_1xrpm_amplitude;
    float    zscore_kurtosis;
    float    zscore_rms;
    float    zscore_bin;
    bool     alert_active;
    bool     warmup_active;
    // TODO: adicionar uint16_t fft_magnitudes[1024] quando dsp_pipeline existir
} dsp_result_t;

// ─── Contexto compartilhado do sistema ───────────────────────────────────────
// Criado UMA vez em main.c, passado por ponteiro para todas as tasks.
// Nenhum componente usa extern ou variável global — tudo passa por aqui.
typedef struct {
    // Core 0 → Core 1: resultado completo do ciclo DSP
    // len=1, xQueueOverwrite — sempre o valor mais recente, sem acumular
    QueueHandle_t     queue_dsp_result;

    // Core 1 → Core 0: RPM medido pelo PCNT a cada 100 ms
    // _Atomic: thread-safe sem mutex para escalar de 32 bits
    _Atomic float     current_rpm;

    // SPI2: compartilhado entre LIS3DH (Core 0) e SD card (Core 1)
    // Mutex com herança de prioridade — evita priority inversion
    // Gerenciado internamente por storage — não acessar diretamente fora dele
    SemaphoreHandle_t mutex_spi2;

} app_context_t;

// ─── Inicialização ────────────────────────────────────────────────────────────
// Chamar UMA vez em app_main(), antes de criar qualquer task.
// Aloca queue, mutex e inicializa o atomic. Retorna ESP_OK ou ESP_ERR_NO_MEM.
esp_err_t app_context_init(app_context_t *ctx);

#ifdef __cplusplus
}
#endif