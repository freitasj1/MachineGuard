/**
 * @file hmi.h
 * @brief Interface da task de interface humano-máquina (HMI)
 * @details Display, botões e LED. Executa no Core 1 com prioridade 10.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de interface humano-máquina
 * @param arg Ponteiro para app_context_t (contexto compartilhado)
 * @details Executa no Core 1 com prioridade 10.
 *          Gerencia display, botões e indicadores visuais.
 *          Recebe dados da queue_dsp_result para atualizar interface.
 */
void task_hmi(void *arg);

#ifdef __cplusplus
}
#endif