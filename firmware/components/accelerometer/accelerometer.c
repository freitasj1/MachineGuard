/**
 * @file accelerometer.c
 * @brief LSM6DS3TR-C SPI/FIFO acquisition task.
 */

#include "accelerometer.h"

#include <stdint.h>
#include <string.h>

#include "app_context.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portmacro.h"

/* SPI2 is initialized once by main.c. GPIO10 is the sensor CS pin. */
#define ACCEL_SPI_HOST                 SPI2_HOST
#define ACCEL_SPI_CS_GPIO              GPIO_NUM_10
#define ACCEL_SPI_CLOCK_HZ             (10 * 1000 * 1000)
#define ACCEL_SPI_MODE                 0

#define ACCEL_BUFFER_SIZE              ACCEL_BLOCK_SIZE
#define ACCEL_FIFO_SAMPLES_PER_READ    64U
#define ACCEL_FIFO_WORDS_PER_SAMPLE    3U
#define ACCEL_FIFO_READ_BYTES          \
    (ACCEL_FIFO_SAMPLES_PER_READ * ACCEL_FIFO_WORDS_PER_SAMPLE * sizeof(int16_t))
// #define ACCEL_FIFO_WATERMARK_WORDS     
//     (ACCEL_FIFO_SAMPLES_PER_READ * ACCEL_FIFO_WORDS_PER_SAMPLE)
#define ACCEL_FIFO_WATERMARK_WORDS (ACCEL_FIFO_SAMPLES_PER_READ * ACCEL_FIFO_WORDS_PER_SAMPLE)

#define ACCEL_AXIS_X                   0U
#define ACCEL_AXIS_Y                   1U
#define ACCEL_AXIS_Z                   2U
#define ACCEL_SELECTED_AXIS            ACCEL_AXIS_Z

#define LSM6DS3TR_C_WHO_AM_I_REG       0x0F
#define LSM6DS3TR_C_WHO_AM_I_VALUE     0x6A
#define LSM6DS3TR_C_CTRL1_XL           0x10
#define LSM6DS3TR_C_CTRL3_C            0x12
#define LSM6DS3TR_C_FIFO_CTRL1         0x06
#define LSM6DS3TR_C_FIFO_CTRL2         0x07
#define LSM6DS3TR_C_FIFO_CTRL3         0x08
#define LSM6DS3TR_C_FIFO_CTRL4         0x09
#define LSM6DS3TR_C_FIFO_CTRL5         0x0A
#define LSM6DS3TR_C_FIFO_STATUS1       0x3A
#define LSM6DS3TR_C_FIFO_STATUS2       0x3B
#define LSM6DS3TR_C_FIFO_DATA_OUT_L    0x3E

#define LSM6DS3TR_C_CTRL3_SW_RESET     (1U << 0)
#define LSM6DS3TR_C_CTRL3_IF_INC       (1U << 2)
#define LSM6DS3TR_C_CTRL3_BDU          (1U << 6)
#define LSM6DS3TR_C_ODR_XL_6K66_HZ     (0xAU << 4)
#define LSM6DS3TR_C_FS_XL_2G           (0x0U << 2)
#define LSM6DS3TR_C_DEC_FIFO_XL_NO_DECIMATION (1U << 0)
#define LSM6DS3TR_C_ODR_FIFO_6K66_HZ   (0xAU << 3)
#define LSM6DS3TR_C_FIFO_MODE_BYPASS   0x00U
#define LSM6DS3TR_C_FIFO_MODE_CONTINUOUS 0x06U
#define LSM6DS3TR_C_FIFO_STATUS2_DIFF_FIFO_MASK 0x07U
#define LSM6DS3TR_C_FIFO_STATUS2_OVERRUN (1U << 6)
#define LSM6DS3TR_C_SPI_READ           (1U << 7)
// #define LSM6DS3TR_C_SPI_AUTO_INCREMENT (1U << 6)

static const char *TAG = "accelerometer";

/** Private ping-pong buffers; they never live in app_context. */
typedef struct {
    accel_block_t ping;
    accel_block_t pong;
    accel_block_t *write;
    accel_block_t *process;
    uint16_t index;
    bool ping_active;
} accel_data_t;

static app_context_t *s_ctx;
static spi_device_handle_t s_spi_device;
static accel_data_t s_data;

/* The SPI driver uses these DMA-capable buffers for queued FIFO transactions. */
DMA_ATTR static uint8_t s_fifo_tx[1 + ACCEL_FIFO_READ_BYTES];
DMA_ATTR static uint8_t s_fifo_rx[1 + ACCEL_FIFO_READ_BYTES];
static spi_transaction_t s_fifo_transaction;

static esp_err_t read_register(uint8_t reg, uint8_t *value);
static esp_err_t write_register(uint8_t reg, uint8_t value);
static esp_err_t sensor_reset(void);
static esp_err_t sensor_configure(void);
static esp_err_t fifo_configure(void);
static esp_err_t fifo_get_level(uint16_t *word_count, bool *overrun);
static esp_err_t fifo_read_samples(uint16_t sample_count);
static void process_fifo_samples(uint16_t sample_count);
static void publish_completed_buffer(void);

esp_err_t accel_init(app_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (context->mutex_spi2 == NULL || context->queue_accel_block == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_spi_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx = context;
    memset(&s_data, 0, sizeof(s_data));
    s_data.write = &s_data.ping;
    s_data.process = &s_data.pong;
    s_data.ping_active = true;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = ACCEL_SPI_CLOCK_HZ,
        .mode = ACCEL_SPI_MODE,
        .spics_io_num = ACCEL_SPI_CS_GPIO,
        .queue_size = 1,
    };

    if (xSemaphoreTake(s_ctx->mutex_spi2, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    esp_err_t err = spi_bus_add_device(ACCEL_SPI_HOST, &device_config, &s_spi_device);
    xSemaphoreGive(s_ctx->mutex_spi2);
    if (err != ESP_OK) {
        return err;
    }

    err = sensor_configure();
    if (err != ESP_OK) {
        xSemaphoreTake(s_ctx->mutex_spi2, portMAX_DELAY);
        spi_bus_remove_device(s_spi_device);
        xSemaphoreGive(s_ctx->mutex_spi2);
        s_spi_device = NULL;
        return err;
    }

    ESP_LOGI(TAG, "LSM6DS3TR-C ready: 6.66 kHz, +/-2g, FIFO continuous");
    return ESP_OK;
}

void task_accel(void *arg)
{
    esp_err_t err = accel_init((app_context_t *)arg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "accelerometer init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        uint16_t fifo_words;
        bool fifo_overrun = false;
        err = fifo_get_level(&fifo_words, &fifo_overrun);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "FIFO status read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ESP_LOGD(TAG, "fifo_words = %u", fifo_words);
        if (fifo_words < ACCEL_FIFO_WATERMARK_WORDS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint16_t sample_count = fifo_words / ACCEL_FIFO_WORDS_PER_SAMPLE;
        if (sample_count > ACCEL_FIFO_SAMPLES_PER_READ) {
            sample_count = ACCEL_FIFO_SAMPLES_PER_READ;
        }
        if (sample_count == 0U) {
            continue;
        }

        err = fifo_read_samples(sample_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "FIFO DMA read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        process_fifo_samples(sample_count);
    }
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    if (value == NULL || s_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx[2] = { (uint8_t)(reg | LSM6DS3TR_C_SPI_READ), 0 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t transaction = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    if (xSemaphoreTake(s_ctx->mutex_spi2, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    esp_err_t err = spi_device_transmit(s_spi_device, &transaction);
    xSemaphoreGive(s_ctx->mutex_spi2);
    if (err == ESP_OK) {
        *value = rx[1];
    }
    return err;
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    if (s_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx[2] = { reg, value };
    spi_transaction_t transaction = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
    };

    if (xSemaphoreTake(s_ctx->mutex_spi2, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    esp_err_t err = spi_device_transmit(s_spi_device, &transaction);
    xSemaphoreGive(s_ctx->mutex_spi2);
    return err;
}

static esp_err_t sensor_reset(void)
{
    esp_err_t err = write_register(LSM6DS3TR_C_CTRL3_C, LSM6DS3TR_C_CTRL3_SW_RESET);
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t attempt = 0; attempt < 50; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(1));
        uint8_t ctrl3;
        err = read_register(LSM6DS3TR_C_CTRL3_C, &ctrl3);
        if (err != ESP_OK) {
            return err;
        }
        if ((ctrl3 & LSM6DS3TR_C_CTRL3_SW_RESET) == 0U) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t sensor_configure(void)
{
    uint8_t who_am_i;
    esp_err_t err = read_register(LSM6DS3TR_C_WHO_AM_I_REG, &who_am_i);
    if (err != ESP_OK) {
        return err;
    }
    if (who_am_i != LSM6DS3TR_C_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "unexpected WHO_AM_I: 0x%02X", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    err = sensor_reset();
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_CTRL3_C,
                         LSM6DS3TR_C_CTRL3_BDU | LSM6DS3TR_C_CTRL3_IF_INC);
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_CTRL1_XL,
                         LSM6DS3TR_C_ODR_XL_6K66_HZ | LSM6DS3TR_C_FS_XL_2G);
    if (err != ESP_OK) {
        return err;
    }
    return fifo_configure();
}

static esp_err_t fifo_configure(void)
{
    esp_err_t err = write_register(LSM6DS3TR_C_FIFO_CTRL5, LSM6DS3TR_C_FIFO_MODE_BYPASS);
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_FIFO_CTRL1,
                         (uint8_t)(ACCEL_FIFO_WATERMARK_WORDS & 0xFFU));
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_FIFO_CTRL2,
                         (uint8_t)((ACCEL_FIFO_WATERMARK_WORDS >> 8) & 0x07U));
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_FIFO_CTRL3, LSM6DS3TR_C_DEC_FIFO_XL_NO_DECIMATION);
    if (err != ESP_OK) {
        return err;
    }
    err = write_register(LSM6DS3TR_C_FIFO_CTRL4, 0);
    if (err != ESP_OK) {
        return err;
    }
    return write_register(LSM6DS3TR_C_FIFO_CTRL5,
                          LSM6DS3TR_C_ODR_FIFO_6K66_HZ |
                          LSM6DS3TR_C_FIFO_MODE_CONTINUOUS);
}

static esp_err_t fifo_get_level(uint16_t *word_count, bool *overrun)
{
    if (word_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status1;
    uint8_t status2;
    esp_err_t err = read_register(LSM6DS3TR_C_FIFO_STATUS1, &status1);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(LSM6DS3TR_C_FIFO_STATUS2, &status2);
    if (err != ESP_OK) {
        return err;
    }

    bool overrun_detected = (status2 & LSM6DS3TR_C_FIFO_STATUS2_OVERRUN) != 0U;
    if (overrun_detected) {
        ESP_LOGW(TAG, "FIFO overrun; oldest samples were discarded");
    }

    if (overrun != NULL) {
        *overrun = overrun_detected;
    }

    *word_count = ((uint16_t)(status2 & LSM6DS3TR_C_FIFO_STATUS2_DIFF_FIFO_MASK) << 8) |
                  status1;
    return ESP_OK;
}

static esp_err_t fifo_read_samples(uint16_t sample_count)
{
    if (sample_count == 0U || sample_count > ACCEL_FIFO_SAMPLES_PER_READ) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t data_bytes = sample_count * ACCEL_FIFO_WORDS_PER_SAMPLE * sizeof(int16_t);
    memset(s_fifo_tx, 0, data_bytes + 1U);
    s_fifo_tx[0] = LSM6DS3TR_C_FIFO_DATA_OUT_L | LSM6DS3TR_C_SPI_READ;
    memset(&s_fifo_transaction, 0, sizeof(s_fifo_transaction));
    s_fifo_transaction.length = (data_bytes + 1U) * 8U;
    s_fifo_transaction.tx_buffer = s_fifo_tx;
    s_fifo_transaction.rx_buffer = s_fifo_rx;

    if (xSemaphoreTake(s_ctx->mutex_spi2, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    esp_err_t err = spi_device_queue_trans(s_spi_device, &s_fifo_transaction, portMAX_DELAY);
    if (err == ESP_OK) {
        spi_transaction_t *result;
        err = spi_device_get_trans_result(s_spi_device, &result, portMAX_DELAY);
    }
    xSemaphoreGive(s_ctx->mutex_spi2);
    return err;
}

static void process_fifo_samples(uint16_t sample_count)
{
    for (uint16_t sample = 0; sample < sample_count; ++sample) {
        const size_t offset = 1U + sample * ACCEL_FIFO_WORDS_PER_SAMPLE * sizeof(int16_t) +
                              ACCEL_SELECTED_AXIS * sizeof(int16_t);
        const accel_sample_t selected = (accel_sample_t)((uint16_t)s_fifo_rx[offset] |
            ((uint16_t)s_fifo_rx[offset + 1U] << 8));
        s_data.write->samples[s_data.index++] = selected;
        if (s_data.index == ACCEL_BLOCK_SIZE) {
            publish_completed_buffer();
        }
    }
}

static void publish_completed_buffer(void)
{
    accel_block_t *completed = s_data.write;
    s_data.write = s_data.process;
    s_data.process = completed;
    s_data.index = 0;
    s_data.ping_active = (s_data.write == &s_data.ping);

    ESP_LOGI(TAG,
         "block: [%d, %d, %d, %d, %d, %d, %d, %d]",
         s_data.process[0],
         s_data.process[1],
         s_data.process[2],
         s_data.process[3],
         s_data.process[4],
         s_data.process[5],
         s_data.process[6],
         s_data.process[7]);

    if (xQueueOverwrite(s_ctx->queue_accel_block, s_data.process) != pdPASS) {
        ESP_LOGW(TAG, "accelerometer block queue overwrite failed");
    }
}
