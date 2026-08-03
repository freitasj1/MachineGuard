/**
 * @file sensors.c
 * @brief Temperature and battery sensor task.
 */

#include "sensors.h"

#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "sensors";

void task_sensors(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_sensors started");
    while (true) {
        /* TODO: read DS18B20 and battery. */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
