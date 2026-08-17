/**
 * @file main.c
 * @brief Inicialização do sistema MachineGuard
 */

#include <stdio.h>

#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "app_context.h"
#include "accelerometer.h"
#include "dac.h"
#include "dsp.h"
#include "hal/spi_types.h"
#include "hmi.h"
#include "sensors.h"
#include "soc/gpio_num.h"
#include "storage.h"
#include "system.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"


/* ============================================================================
 * Private constants and macros
 * ========================================================================== */

static const char *TAG = "main";  /**< Tag para logs ESP-IDF */

#define SPI2_HOST_USED      SPI2_HOST

#define SPI2_PIN_MOSI    GPIO_NUM_11
#define SPI2_PIN_MISO    GPIO_NUM_13
#define SPI2_PIN_SCLK    GPIO_NUM_12   

#define SPI_DMA_CHAN        SPI_DMA_CH_AUTO

static app_context_t ctx;           /**< Contexto compartilhado do sistema */
/* ============================================================================
 * Public types
 * ========================================================================== */



/* ============================================================================
 * Public variables
 * ========================================================================== */



/* ============================================================================
 * Private variables
 * ========================================================================== */



/* ============================================================================
 * Private function prototypes
 * ========================================================================== */

 static esp_err_t spi2_bus_init(void);

/* ============================================================================
 * Public function implementations
 * ========================================================================== */

/**
 * @brief Ponto de entrada da aplicação
 */
void app_main(void)
{
    ESP_LOGI(TAG, "MachineGuard iniciando...");

    ESP_ERROR_CHECK(app_context_init(&ctx));

    ESP_ERROR_CHECK(spi2_bus_init());

    // função da task, nome de debug, stack em bytes, parâmetro(ponteiro para o contexto), prioridade, handle (NULL), core
    xTaskCreatePinnedToCore(task_dsp,     "dsp",     8192, &ctx, 23, NULL, 0);
    xTaskCreatePinnedToCore(task_system,  "system",  8192, &ctx, 22, NULL, 0);
    
    xTaskCreatePinnedToCore(task_accel,   "accel",   4096, &ctx, 24, NULL, 1);
    xTaskCreatePinnedToCore(task_sensors, "sensors", 4096, &ctx, 12, NULL, 1);
    xTaskCreatePinnedToCore(task_hmi,  "hmi",  4096, &ctx, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_dac,  "dac",  4096, &ctx,  9, NULL, 1);
    xTaskCreatePinnedToCore(task_sd,   "sd",   4096, &ctx,  8, NULL, 1);
    
}
 
/* ============================================================================
 * Private function implementations
 * ========================================================================== */

static esp_err_t spi2_bus_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI2_PIN_MOSI,
        .miso_io_num = SPI2_PIN_MISO,
        .sclk_io_num = SPI2_PIN_SCLK,

        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        .max_transfer_sz = 4096,
    };

    esp_err_t err = spi_bus_initialize(
        SPI2_HOST_USED,
        &bus_cfg,
        SPI_DMA_CHAN);

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to initialize SPI2 bus: %s",
                 esp_err_to_name(err));
    }

    return err;
}