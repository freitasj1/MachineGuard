/**
 * @file accelerometer.c
 * @brief Stub driver para LSM6DS3TR-C (leitura bruta de amostras)
 */

#include "accelerometer.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "accelerometer";

esp_err_t accel_init(SemaphoreHandle_t spi2_mutex)
{
    (void)spi2_mutex;
    ESP_LOGI(TAG, "accel_init (stub) called");
    return ESP_OK;
}

esp_err_t accel_read_raw(accel_sample_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    // Retorna zeros (stub). Substituir por leitura SPI real.
    out->x = 0;
    out->y = 0;
    out->z = 0;
    return ESP_OK;
}
