/**
 * @file app_context.h
 * @brief Contexto compartilhado do sistema MachineGuard
 * @details Define as estruturas de dados centrais (app_context_t, dsp_result_t)
 *          e a função de inicialização. Todas as tasks recebem um ponteiro para
 *          o contexto, eliminando a necessidade de variáveis globais.
 */

#pragma once

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct dsp_result_t
 * @brief Resultado de um ciclo DSP completo
 * @details Produzido pelo Core 0 (task_dsp) e consumido pelo Core 1
 *          (task_hmi, task_sd, task_dac) via queue_dsp_result.
 */
typedef struct {
    float    kurtosis;                /**< Curtose do sinal */
    float    rms;                     /**< RMS (Root Mean Square) */
    float    crest_factor;            /**< Fator de crista */
    float    bin_1xrpm_amplitude;     /**< Amplitude do bin 1x RPM */
    float    zscore_kurtosis;         /**< Z-score da curtose */
    float    zscore_rms;              /**< Z-score do RMS */
    float    zscore_bin;              /**< Z-score do bin */
    bool     alert_active;            /**< Alerta ativo */
    bool     warmup_active;           /**< Aquecimento ativo */
} dsp_result_t;

/**
 * @brief Tipo de amostra bruta do acelerômetro (int16)
 */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} accel_sample_t;

/** Tamanho da janela de amostras para o DSP */
enum { ACCEL_WINDOW_SIZE = 256 };

/** Sensibilidade padrão do acelerômetro em mg/LSB (±2g) */
#define ACCEL_MG_PER_LSB 0.061f

typedef struct {
    accel_sample_t samples[ACCEL_WINDOW_SIZE];
    size_t head;   /* índice de escrita */
    size_t count;  /* número de amostras presentes */
} accel_input_buffer_t;

/**
 * @struct app_context_t
 * @brief Contexto compartilhado entre todas as tasks
 * @details Criado UMA vez em main.c, passado por ponteiro para todas as tasks.
 *          Nenhum componente usa extern ou variável global — tudo passa por aqui.
 */
typedef struct {
    /**
     * @brief Queue de resultados DSP (Core 0 → Core 1)
     * @details Tamanho=1, overwrite — sempre o valor mais recente, sem acumular
     */
    QueueHandle_t     queue_dsp_result;

    /**
     * @brief RPM atual medido pelo PCNT
     * @details _Atomic: thread-safe sem mutex para escalar de 32 bits
     *          Atualizado por Core 1, lido por Core 0
     */
    _Atomic float     current_rpm;

    /**
     * @brief Mutex para SPI2 compartilhado
     * @details Compartilhado entre LIS3DH (Core 0) e SD card (Core 1).
     *          Mutex com herança de prioridade — evita priority inversion.
     *          Gerenciado internamente por storage — não acessar diretamente.
     */
    SemaphoreHandle_t mutex_spi2;

    /**
     * @brief Mutex para proteger o buffer de amostras do acelerômetro
     */
    SemaphoreHandle_t mutex_accel_buffer;

    accel_input_buffer_t accel_input;

} app_context_t;

/**
 * @brief Inicializa o contexto compartilhado do sistema
 * @param ctx Ponteiro para estrutura app_context_t a ser inicializada
 * @return ESP_OK se bem-sucedido, ESP_ERR_NO_MEM se falha em alocar recursos
 * @details Chamar UMA vez em app_main(), antes de criar qualquer task.
 *          Aloca queue, mutex e inicializa o atomic.
 */
esp_err_t app_context_init(app_context_t *ctx);

#ifdef __cplusplus
}
#endif