/**
 * @file fft.h
 * @brief Interface da tela de espectro FFT.
 */

#ifndef FFT_H
#define FFT_H

#include "app_context.h"
#include "esp_err.h"

/**
 * @brief Inicializa a tela FFT.
 *
 * @return ESP_OK em caso de sucesso.
 * @return Código de erro caso a inicialização falhe.
 */
esp_err_t fft_init(void);

/**
 * @brief Atualiza o gráfico FFT.
 *
 * @param data Dados disponibilizados pelo sistema para a HMI.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se data for NULL.
 * @return ESP_ERR_INVALID_STATE se a tela não estiver inicializada.
 */
esp_err_t fft_update(
    const hmi_data_t *data
);

#endif /* FFT_H */
