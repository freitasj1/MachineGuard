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
#include "driver/gpio.h"
#include "hal/gpio_types.h"

static const char *TAG = "hmi";

#define LED_GPIO 48

void task_hmi(void *arg)
{
    (void)arg;

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

    while (1)
    {
        // TODO:
        // 1. Ler dados da fila queue_dsp_result
        // 2. Atualizar display
        // 3. Processar botões
        // 4. Atualizar LEDs de status

        led_ligado = !led_ligado;
        gpio_set_level(LED_GPIO, led_ligado);

        ESP_LOGD(TAG, "Alternando estado do LED");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}