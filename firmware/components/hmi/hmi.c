/**
 * @file hmi.c
 * @brief Implementação da task HMI
 */

#include "hmi.h"
#include "app_context.h"
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

        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT, // IMPORTANTE: permite ler e escrever
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

     bool led_ligado = false;

    while (1) {
        // TODO: Implementar HMI:
        // 1. Ler dados de queue_dsp_result
        // 2. Atualizar display com valores
        // 3. Processar entrada de botões
        // 4. Controlar LEDs de status
        led_ligado = !led_ligado; // Inverte a variável
        gpio_set_level(LED_GPIO, led_ligado);
        ESP_LOGD(TAG, "Alternando estado do led");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}