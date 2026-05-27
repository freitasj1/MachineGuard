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
