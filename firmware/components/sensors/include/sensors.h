/**
 * @file sensors.h
 * @brief Interface para coleta de dados de sensores (acelerômetro + RPM)
 * @details Agrupa a leitura de múltiplos sensores em uma única task
 *          executada no Core 1 com prioridade 12.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de coleta de sensores
 * @param arg Ponteiro para app_context_t (contexto compartilhado)
 * @details Executa no Core 1 com prioridade 12. Coleta dados de:
 *          - Acelerômetro LIS3DH via SPI2
 *          - Contador de RPM via PCNT
 *          Atualiza o contexto compartilhado com os dados coletados.
 */
void task_sensors(void *arg);

#ifdef __cplusplus
}
#endif