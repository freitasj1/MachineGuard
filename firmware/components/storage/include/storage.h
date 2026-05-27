/**
 * @file storage.h
 * @brief Interface da task de armazenamento em SD Card
 * @details Escreve dados em cartão SD. Executa no Core 1 com prioridade 8.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de armazenamento em SD card
 * @param arg Ponteiro para app_context_t (contexto compartilhado)
 * @details Executa no Core 1 com prioridade 8 (mais baixa).
 *          Lê dados de queue_dsp_result e os escreve em arquivo no SD card.
 *          Gerencia mutex_spi2 para acesso exclusivo ao barramento.
 */
void task_sd(void *arg);

#ifdef __cplusplus
}
#endif