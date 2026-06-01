/**
 * @file accelerometer.h
 * @brief Interface do módulo acelerômetro LIS3DH
 * @details Abstrações para inicialização e leitura do acelerômetro.
 *          Acesso via SPI2 (compartilhado com SD card).
 *          Chamado pela task_sensors no Core 0.
 */

#pragma once

#include "app_context.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensibilidade do acelerômetro em mg/LSB (padrão: ±2g → 0.061 mg/LSB)
 */
#define ACCEL_MG_PER_LSB 0.061f

/**
 * @note `accel_sample_t` é definido em `app_context.h` e representa a amostra bruta int16.
 */

/**
 * @brief Inicializa o driver do LSM6DS3TR-C
 * @param spi2_mutex Mutex do barramento SPI2 (pode ser NULL se não usado)
 */
esp_err_t accel_init(SemaphoreHandle_t spi2_mutex);

/**
 * @brief Lê uma amostra bruta do acelerômetro (int16)
 * @param out Ponteiro para accel_sample_t a ser preenchido
 */
esp_err_t accel_read_raw(accel_sample_t *out);

#ifdef __cplusplus
}
#endif
/**
 * @file accelerometer.h
 * @brief Interface do módulo acelerômetro LIS3DH
 * @details Abstrações para inicialização e leitura do acelerômetro.
 *          Acesso via SPI2 (compartilhado com SD card).
 *          Chamado pela task_sensors no Core 0.
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Declarar estrutura de dados de aceleração
// typedef struct {
//     float x, y, z;  // Aceleração nos eixos X, Y, Z
// } accel_data_t;

// TODO: Declarar funções de inicialização e leitura
// - esp_err_t accel_init(SemaphoreHandle_t spi2_mutex)
// - esp_err_t accel_read(accel_data_t *data)
// - esp_err_t accel_self_test(void)

#ifdef __cplusplus
}
#endif
