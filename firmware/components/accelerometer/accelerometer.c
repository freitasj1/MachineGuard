/**
 * @file accelerometer.c
 * @brief Driver stub de acelerômetro
 */

#include "accelerometer.h"
#include "app_context.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "portmacro.h"
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"

/* ============================================================================
* Private constants and macros
* ========================================================================== */

#define ACCEL_BUFFER_SIZE 2048

static const char *TAG = "accelerometer";


/* ============================================================================
 * Private types
 * ========================================================================== */
/**
 * @brief Buffer duplo de aquisição.
 *
 * O driver escreve continuamente em um buffer enquanto o outro
 * permanece disponível para processamento pelo DSP.
 *
 * Esta estrutura é privada do módulo accelerometer.
 */
typedef struct
{
    accel_sample_t ping[ACCEL_BUFFER_SIZE];
    accel_sample_t pong[ACCEL_BUFFER_SIZE];

    accel_sample_t *write;
    accel_sample_t *process;

    uint16_t index;

    bool ping_active;

} accel_stream_t;

/* ============================================================================
 * Public variables
 * ========================================================================== */



/* ============================================================================
 * Private variables
 * ========================================================================== */

static spi_device_handle_t accel_spi;

static app_context_t *ctx;

/* ============================================================================
 * Private function prototypes
 * ========================================================================== */

static esp_err_t sensor_reset(void);

static esp_err_t sensor_config(void);
static esp_err_t read_register(uint8_t reg, uint8_t *data, size_t len);

static esp_err_t write_register(uint8_t reg, const uint8_t *data, size_t len);


/* ============================================================================
 * Public function implementations
 * ========================================================================== */

esp_err_t accel_init(app_context_t *context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    
    ctx = context;

   
    return ESP_OK;
}
void task_accel(void *arg)
{
    esp_err_t err_accel;
    err_accel = accel_init((app_context_t *)arg);
    if(err_accel != ESP_OK) ESP_LOGE(TAG, "accelerometer init failed, err code = %u", err_accel);


    while (1)
    {
        if (xSemaphoreTake(ctx->mutex_spi2,
                           portMAX_DELAY) == pdTRUE)
        {
            /* Futuramente SPI */

            xSemaphoreGive(ctx->mutex_spi2);
        }
    }
}


/* ============================================================================
 * Private function implementations
 * ========================================================================== */