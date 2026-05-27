/**
 * @file rpm.h
 * @brief Interface do módulo contador de RPM via PCNT
 * @details Fornece abstrações para inicialização e leitura do contador
 *          de pulsos. Chamado pela task_sensors no Core 1.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Declarar funções de inicialização e leitura de RPM
// - esp_err_t rpm_init(void)      - Configura PCNT
// - float rpm_read(void)          - Retorna RPM atual

#ifdef __cplusplus
}
#endif