/**
 * @file dsp.h
 * @brief Interface da task de processamento digital de sinais (DSP)
 * @details Executa no Core 0 com prioridade máxima (24).
 *          Processa sinais de aceleração e gera resultados para fila compartilhada.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de processamento DSP
 * @param arg Ponteiro para app_context_t (contexto compartilhado)
 * @details Executa no Core 0 com prioridade 24 (máxima).
 *          Realiza análise espectral, cálculos estatísticos e detecção de anomalias.
 *          Envia resultados via queue_dsp_result para Core 1.
 */
void task_dsp(void *arg);

#ifdef __cplusplus
}
#endif