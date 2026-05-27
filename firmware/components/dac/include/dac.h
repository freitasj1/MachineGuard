/**
 * @file dac.h
 * @brief Interface da task de conversão digital-analógica (DAC)
 * @details Controle de áudio/alertas. Executa no Core 1 com prioridade 9.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de DAC e alertas sonoros
 * @param arg Ponteiro para app_context_t (contexto compartilhado)
 * @details Executa no Core 1 com prioridade 9.
 *          Gera alertas sonoros baseado em dados de queue_dsp_result.
 *          Converte sinais digitais em saída analógica.
 */
void task_dac(void *arg);

#ifdef __cplusplus
}
#endif