/**
 * @file hmi.c
 * @brief Implementação da task HMI
 */

#include "hmi.h"
#include "app_context.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

static const char *TAG = "hmi";

#define LED_GPIO 48

void task_hmi(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL || ctx->queue_system_to_hmi == NULL) {
        ESP_LOGE(TAG, "invalid HMI context");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "task_hmi iniciada");

    // Configuração do GPIO do LED
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    bool led_ligado = false;
    TickType_t last_log_tick = 0U;

    while (true) {
        hmi_data_t data;

        if (xQueueReceive(ctx->queue_system_to_hmi,
                          &data,
                          pdMS_TO_TICKS(250)) != pdTRUE) {
            continue;
        }

        led_ligado = !led_ligado;
        gpio_set_level(LED_GPIO, led_ligado);

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_log_tick) >= pdMS_TO_TICKS(1000)) {
            ESP_LOGI(TAG,
                     "state=%d | warm-up=%u/%u | RPM=%.1f | RMS=%.5f | FFT=%u points",
                     data.state.state,
                     data.warmup.evaluations,
                     data.warmup.required,
                     data.features.rpm,
                     data.features.rms,
                     HMI_FFT_POINT_COUNT);
            last_log_tick = now;
        }
    }
}
