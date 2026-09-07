/**
 * @file status.h
 * @brief Tela principal de status do MachineGuard.
 */

#pragma once

#include "app_context.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa a tela STATUS.
 *
 * Desenha todos os elementos estáticos da interface.
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t status_init(void);

/**
 * @brief Atualiza os elementos dinâmicos da tela STATUS.
 *
 * Somente regiões cujo valor visual mudou são redesenhadas.
 *
 * @param data Dados recebidos do sistema.
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t status_update(const hmi_data_t *data);

#ifdef __cplusplus
}
#endif