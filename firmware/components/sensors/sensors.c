/**
 * @file sensors.c
 * @brief Implementação da task de coleta de sensores
 */

#include "sensors.h"
#include "app_context.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
/* accel_read_raw may be implemented in the accelerometer component; declare
 * it weakly so linking still succeeds if the driver is omitted during testing.
 */
extern esp_err_t accel_read_raw(accel_sample_t *out) __attribute__((weak));
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensors";

void task_sensors(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_sensors iniciada");
    while (1) {
        // TODO: ler acelerômetro e RPM e atualizar contexto
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}