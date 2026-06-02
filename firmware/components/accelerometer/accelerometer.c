/**
 * @file accelerometer.c
 * @brief Driver stub de acelerômetro
 */

#include "accelerometer.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "accelerometer";

esp_err_t accel_init(SemaphoreHandle_t spi2_mutex)
{
    (void)spi2_mutex;
    ESP_LOGI(TAG, "accel_init chamado");
    // TODO: inicializar acelerômetro e SPI2
    return ESP_OK;
}

esp_err_t accel_read_raw(accel_sample_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    out->x = 0;
    out->y = 0;
    out->z = 0;
    // TODO: ler dados reais do acelerômetro
    return ESP_OK;
}
