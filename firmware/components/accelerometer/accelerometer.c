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
#define ACCEL_SPI_CS_GPIO              GPIO_NUM_13
#define ACCEL_SPI_CLOCK_HZ             (10 * 1000 * 1000)
#define ACCEL_SPI_MODE                 0

/*
 * SPI operations must not wait indefinitely.
 *
 * This is especially important for the recovery mechanism:
 * if the SPI driver/bus becomes unavailable, task_accel must
 * return from the SPI call instead of remaining blocked forever.
 */
#define ACCEL_SPI_MUTEX_TIMEOUT_MS     50U
#define ACCEL_SPI_QUEUE_TIMEOUT_MS     50U
#define ACCEL_SPI_RESULT_TIMEOUT_MS    50U

#define ACCEL_SPI_MUTEX_TIMEOUT_TICKS  \
    pdMS_TO_TICKS(ACCEL_SPI_MUTEX_TIMEOUT_MS)

#define ACCEL_SPI_QUEUE_TIMEOUT_TICKS  \
    pdMS_TO_TICKS(ACCEL_SPI_QUEUE_TIMEOUT_MS)

#define ACCEL_SPI_RESULT_TIMEOUT_TICKS \
    pdMS_TO_TICKS(ACCEL_SPI_RESULT_TIMEOUT_MS)

/* Sensor recovery parameters. */
#define ACCEL_RECOVERY_MAX_ATTEMPTS    3U
#define ACCEL_RECOVERY_RETRY_DELAY_MS  20U
#define ACCEL_RECOVERY_FAILURE_DELAY_MS 100U

#define ACCEL_BUFFER_SIZE              ACCEL_BLOCK_SIZE
#define ACCEL_FIFO_SAMPLES_PER_READ    64U
#define ACCEL_FIFO_WORDS_PER_SAMPLE    3U

#define ACCEL_FIFO_READ_BYTES          \
    (ACCEL_FIFO_SAMPLES_PER_READ * ACCEL_FIFO_WORDS_PER_SAMPLE * sizeof(int16_t))

#define ACCEL_FIFO_WATERMARK_WORDS     \
    (ACCEL_FIFO_SAMPLES_PER_READ * ACCEL_FIFO_WORDS_PER_SAMPLE)

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

#define LSM6DS3TR_C_DEC_FIFO_XL_NO_DECIMATION \
    (1U << 0)

#define LSM6DS3TR_C_ODR_FIFO_6K66_HZ   (0xAU << 3)

#define LSM6DS3TR_C_FIFO_MODE_BYPASS   0x00U
#define LSM6DS3TR_C_FIFO_MODE_CONTINUOUS 0x06U

#define LSM6DS3TR_C_FIFO_STATUS2_DIFF_FIFO_MASK \
    0x07U

#define LSM6DS3TR_C_FIFO_STATUS2_OVERRUN \
    (1U << 6)

#define LSM6DS3TR_C_SPI_READ           (1U << 7)

static const char *TAG = "accelerometer";

/**
 * @brief Private ping-pong buffers.
 *
 * They never live in app_context.
 */
typedef struct {
    accel_block_t ping;
    accel_block_t pong;

    accel_block_t *write;
    accel_block_t *process;

    uint16_t index;
    bool ping_active;
} accel_data_t;

/**
 * @brief Type of transaction currently owned by the SPI driver.
 *
 * This is required because spi_device_get_trans_result() must eventually
 * retrieve a transaction queued with spi_device_queue_trans().
 */
typedef enum {
    ACCEL_SPI_PENDING_NONE = 0,
    ACCEL_SPI_PENDING_REGISTER,
    ACCEL_SPI_PENDING_FIFO
} accel_spi_pending_t;

static app_context_t *s_ctx;
static spi_device_handle_t s_spi_device;
static accel_data_t s_data;

/*
 * Register transaction.
 *
 * These objects are static because a transaction descriptor and its
 * buffers must remain valid until spi_device_get_trans_result()
 * returns the transaction to the application.
 */
static uint8_t s_register_tx[2];
static uint8_t s_register_rx[2];
static spi_transaction_t s_register_transaction;

/*
 * FIFO transaction.
 *
 * DMA-capable buffers are required because SPI2 is initialized with DMA.
 */
DMA_ATTR static uint8_t s_fifo_tx[1 + ACCEL_FIFO_READ_BYTES];
DMA_ATTR static uint8_t s_fifo_rx[1 + ACCEL_FIFO_READ_BYTES];
static spi_transaction_t s_fifo_transaction;

/**
 * @brief Transaction currently owned by the SPI driver.
 */
static accel_spi_pending_t s_spi_pending = ACCEL_SPI_PENDING_NONE;

/* Forward declarations. */
static esp_err_t read_register(uint8_t reg, uint8_t *value);
static esp_err_t write_register(uint8_t reg, uint8_t value);

static esp_err_t sensor_reset(void);
static esp_err_t sensor_apply_configuration(void);
static esp_err_t sensor_configure(void);
static esp_err_t sensor_recover(void);

static esp_err_t fifo_configure(void);
static esp_err_t fifo_get_level(uint16_t *word_count, bool *overrun);
static esp_err_t fifo_read_samples(uint16_t sample_count);

static esp_err_t spi_finish_pending_transaction(void);
static esp_err_t spi_execute_transaction(
    spi_transaction_t *transaction,
    accel_spi_pending_t pending_type);

static void process_fifo_samples(uint16_t sample_count);
static void publish_completed_buffer(void);


/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

esp_err_t accel_init(app_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (context->mutex_spi2 == NULL ||
        context->queue_accel_block_to_dsp == NULL) {
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

    memset(&s_register_transaction, 0, sizeof(s_register_transaction));
    memset(s_register_tx, 0, sizeof(s_register_tx));
    memset(s_register_rx, 0, sizeof(s_register_rx));

    memset(&s_fifo_transaction, 0, sizeof(s_fifo_transaction));

    s_spi_pending = ACCEL_SPI_PENDING_NONE;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = ACCEL_SPI_CLOCK_HZ,
        .mode = ACCEL_SPI_MODE,
        .spics_io_num = ACCEL_SPI_CS_GPIO,
        .queue_size = 1,
    };

    if (xSemaphoreTake(
            s_ctx->mutex_spi2,
            ACCEL_SPI_MUTEX_TIMEOUT_TICKS) != pdTRUE) {

        ESP_LOGE(TAG, "SPI2 mutex unavailable during initialization");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = spi_bus_add_device(
        ACCEL_SPI_HOST,
        &device_config,
        &s_spi_device);

    xSemaphoreGive(s_ctx->mutex_spi2);

    if (err != ESP_OK) {
        return err;
    }

    err = sensor_configure();

    if (err != ESP_OK) {
        xSemaphoreTake(
            s_ctx->mutex_spi2,
            ACCEL_SPI_MUTEX_TIMEOUT_TICKS);

        spi_bus_remove_device(s_spi_device);

        xSemaphoreGive(s_ctx->mutex_spi2);

        s_spi_device = NULL;

        return err;
    }
    

    ESP_LOGI(
        TAG,
        "LSM6DS3TR-C ready: 6.66 kHz, +/-2g, FIFO continuous");

    return ESP_OK;
}


/* -------------------------------------------------------------------------- */
/* Acquisition task                                                           */
/* -------------------------------------------------------------------------- */

void task_accel(void *arg)
{
    esp_err_t err = accel_init((app_context_t *)arg);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "accelerometer init failed: %s",
            esp_err_to_name(err));

        vTaskDelete(NULL);
        return;
    }

    while (true) {

        uint16_t fifo_words = 0U;
        bool fifo_overrun = false;

        /*
         * Read FIFO level.
         */
        err = fifo_get_level(&fifo_words, &fifo_overrun);

        if (err != ESP_OK) {

            /*
             * No valid continuity can be guaranteed after a communication
             * error. Discard the block currently being accumulated.
             */
            s_data.index = 0U;

            ESP_LOGW(
                TAG,
                "FIFO status read failed: %s",
                esp_err_to_name(err));

            ESP_LOGW(
                TAG,
                "attempting sensor recovery");

            err = sensor_recover();

            if (err == ESP_OK) {

                ESP_LOGI(
                    TAG,
                    "sensor recovery successful");

                ESP_LOGI(
                    TAG,
                    "acquisition resumed");

                vTaskDelay(pdMS_TO_TICKS(2));

            } else {

                ESP_LOGE(
                    TAG,
                    "sensor recovery failed: %s",
                    esp_err_to_name(err));

                vTaskDelay(
                    pdMS_TO_TICKS(
                        ACCEL_RECOVERY_FAILURE_DELAY_MS));
            }

            continue;
        }

        /*
         * FIFO overrun means that the oldest samples were already lost.
         *
         * Therefore the block currently being constructed is invalid from
         * a temporal-continuity point of view. Instead of merely clearing
         * the index and continuing with the old FIFO contents, perform a
         * complete local sensor recovery.
         */
        if (fifo_overrun) {

            s_data.index = 0U;

            ESP_LOGW(
                TAG,
                "FIFO overrun; oldest samples were discarded");

            ESP_LOGW(
                TAG,
                "discarding incomplete block");

            ESP_LOGW(
                TAG,
                "attempting sensor recovery");

            err = sensor_recover();

            if (err == ESP_OK) {

                ESP_LOGI(
                    TAG,
                    "sensor recovery successful");

                ESP_LOGI(
                    TAG,
                    "acquisition resumed");

                /*
                 * The FIFO was reset/reconfigured, so do not process the
                 * old FIFO level. Start measuring from a clean FIFO state.
                 */
                vTaskDelay(pdMS_TO_TICKS(2));

            } else {

                ESP_LOGE(
                    TAG,
                    "sensor recovery failed: %s",
                    esp_err_to_name(err));

                vTaskDelay(
                    pdMS_TO_TICKS(
                        ACCEL_RECOVERY_FAILURE_DELAY_MS));
            }

            continue;
        }

        /*
         * Wait until enough samples are available for one FIFO transfer.
         */
        if (fifo_words < ACCEL_FIFO_WATERMARK_WORDS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint16_t sample_count =
            fifo_words / ACCEL_FIFO_WORDS_PER_SAMPLE;

        if (sample_count > ACCEL_FIFO_SAMPLES_PER_READ) {
            sample_count = ACCEL_FIFO_SAMPLES_PER_READ;
        }

        if (sample_count == 0U) {
            continue;
        }

        /*
         * Read FIFO data.
         */
        err = fifo_read_samples(sample_count);

        if (err != ESP_OK) {

            /*
             * Do not continue appending to a partially acquired block.
             */
            s_data.index = 0U;

            ESP_LOGW(
                TAG,
                "FIFO DMA read failed: %s",
                esp_err_to_name(err));

            ESP_LOGW(
                TAG,
                "attempting sensor recovery");

            err = sensor_recover();

            if (err == ESP_OK) {

                ESP_LOGI(
                    TAG,
                    "sensor recovery successful");

                ESP_LOGI(
                    TAG,
                    "acquisition resumed");

                vTaskDelay(pdMS_TO_TICKS(2));

            } else {

                ESP_LOGE(
                    TAG,
                    "sensor recovery failed: %s",
                    esp_err_to_name(err));

                vTaskDelay(
                    pdMS_TO_TICKS(
                        ACCEL_RECOVERY_FAILURE_DELAY_MS));
            }

            continue;
        }

        /*
         * Only valid, completely transferred FIFO data reaches here.
         */
        process_fifo_samples(sample_count);
    }
}


/* -------------------------------------------------------------------------- */
/* SPI transaction handling                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Finish a transaction that is still owned by the SPI driver.
 *
 * A transaction descriptor must not be reused until the driver returns it
 * through spi_device_get_trans_result().
 */
static esp_err_t spi_finish_pending_transaction(void)
{
    if (s_spi_pending == ACCEL_SPI_PENDING_NONE) {
        return ESP_OK;
    }

    spi_transaction_t *result = NULL;

    esp_err_t err = spi_device_get_trans_result(
        s_spi_device,
        &result,
        ACCEL_SPI_RESULT_TIMEOUT_TICKS);

    if (err == ESP_OK) {
        s_spi_pending = ACCEL_SPI_PENDING_NONE;
        return ESP_OK;
    }

    return err;
}


/**
 * @brief Queue and wait for a SPI transaction with bounded timeouts.
 */
static esp_err_t spi_execute_transaction(
    spi_transaction_t *transaction,
    accel_spi_pending_t pending_type)
{
    if (transaction == NULL || s_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * If a previous transaction timed out while waiting for its result,
     * finish it before attempting to reuse the device.
     */
    if (s_spi_pending != ACCEL_SPI_PENDING_NONE) {

        esp_err_t err = spi_finish_pending_transaction();

        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = spi_device_queue_trans(
        s_spi_device,
        transaction,
        ACCEL_SPI_QUEUE_TIMEOUT_TICKS);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * The driver now owns the transaction descriptor.
     */
    s_spi_pending = pending_type;

    spi_transaction_t *result = NULL;

    err = spi_device_get_trans_result(
        s_spi_device,
        &result,
        ACCEL_SPI_RESULT_TIMEOUT_TICKS);

    if (err == ESP_OK) {
        s_spi_pending = ACCEL_SPI_PENDING_NONE;
    }

    /*
     * If get_trans_result() timed out, keep s_spi_pending set.
     *
     * The transaction descriptor and its buffers remain static and valid,
     * so they can safely remain owned by the SPI driver until a subsequent
     * call succeeds in retrieving the result.
     */
    return err;
}


/* -------------------------------------------------------------------------- */
/* Register access                                                             */
/* -------------------------------------------------------------------------- */

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    if (value == NULL || s_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_register_tx, 0, sizeof(s_register_tx));
    memset(s_register_rx, 0, sizeof(s_register_rx));

    s_register_tx[0] =
        (uint8_t)(reg | LSM6DS3TR_C_SPI_READ);

    s_register_transaction.length =
        sizeof(s_register_tx) * 8U;

    s_register_transaction.tx_buffer =
        s_register_tx;

    s_register_transaction.rx_buffer =
        s_register_rx;

    if (xSemaphoreTake(
            s_ctx->mutex_spi2,
            ACCEL_SPI_MUTEX_TIMEOUT_TICKS) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = spi_execute_transaction(
        &s_register_transaction,
        ACCEL_SPI_PENDING_REGISTER);

    xSemaphoreGive(s_ctx->mutex_spi2);

    if (err == ESP_OK) {
        *value = s_register_rx[1];
    }

    return err;
}


static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    if (s_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_register_tx, 0, sizeof(s_register_tx));
    memset(s_register_rx, 0, sizeof(s_register_rx));

    s_register_tx[0] = reg;
    s_register_tx[1] = value;

    s_register_transaction.length =
        sizeof(s_register_tx) * 8U;

    s_register_transaction.tx_buffer =
        s_register_tx;

    s_register_transaction.rx_buffer = NULL;

    if (xSemaphoreTake(
            s_ctx->mutex_spi2,
            ACCEL_SPI_MUTEX_TIMEOUT_TICKS) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = spi_execute_transaction(
        &s_register_transaction,
        ACCEL_SPI_PENDING_REGISTER);

    xSemaphoreGive(s_ctx->mutex_spi2);

    return err;
}


/* -------------------------------------------------------------------------- */
/* Sensor reset/configuration                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Perform software reset of the LSM6DS3TR-C.
 */
static esp_err_t sensor_reset(void)
{
    esp_err_t err = write_register(
        LSM6DS3TR_C_CTRL3_C,
        LSM6DS3TR_C_CTRL3_SW_RESET);

    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t attempt = 0; attempt < 50U; ++attempt) {

        vTaskDelay(pdMS_TO_TICKS(1));

        uint8_t ctrl3 = 0U;

        err = read_register(
            LSM6DS3TR_C_CTRL3_C,
            &ctrl3);

        if (err != ESP_OK) {
            return err;
        }

        if ((ctrl3 & LSM6DS3TR_C_CTRL3_SW_RESET) == 0U) {
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}


/**
 * @brief Apply the normal accelerometer and FIFO configuration.
 *
 * This function assumes that the sensor is already accessible over SPI.
 * It intentionally does not perform WHO_AM_I or software reset.
 */
static esp_err_t sensor_apply_configuration(void)
{
    esp_err_t err;

    /*
     * BDU + register auto-increment.
     */
    err = write_register(
        LSM6DS3TR_C_CTRL3_C,
        LSM6DS3TR_C_CTRL3_BDU |
        LSM6DS3TR_C_CTRL3_IF_INC);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Accelerometer:
     * 6.66 kHz ODR
     * +/-2 g full scale
     */
    err = write_register(
        LSM6DS3TR_C_CTRL1_XL,
        LSM6DS3TR_C_ODR_XL_6K66_HZ |
        LSM6DS3TR_C_FS_XL_2G);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * FIFO configuration.
     */
    return fifo_configure();
}


/**
 * @brief Normal initialization of the sensor.
 */
static esp_err_t sensor_configure(void)
{
    uint8_t who_am_i = 0U;

    esp_err_t err = read_register(
        LSM6DS3TR_C_WHO_AM_I_REG,
        &who_am_i);

    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X", who_am_i);
    if (who_am_i != LSM6DS3TR_C_WHO_AM_I_VALUE) {

        ESP_LOGE(
            TAG,
            "unexpected WHO_AM_I: 0x%02X",
            who_am_i);

        return ESP_ERR_NOT_FOUND;
    }

    err = sensor_reset();

    if (err != ESP_OK) {
        return err;
    }

    return sensor_apply_configuration();
}


/**
 * @brief Local sensor recovery.
 *
 * This does NOT reset the ESP32 and does NOT affect the system baseline.
 *
 * The recovery sequence is:
 *
 *   discard partial block
 *        ↓
 *   sensor software reset
 *        ↓
 *   reapply accelerometer configuration
 *        ↓
 *   reconfigure FIFO
 *        ↓
 *   resume acquisition
 */
static esp_err_t sensor_recover(void)
{
    esp_err_t last_error = ESP_FAIL;

    /*
     * Any partial block is invalid after a communication/FIFO failure.
     */
    s_data.index = 0U;

    for (uint8_t attempt = 0U;
         attempt < ACCEL_RECOVERY_MAX_ATTEMPTS;
         ++attempt) {

        if (attempt > 0U) {
            vTaskDelay(
                pdMS_TO_TICKS(
                    ACCEL_RECOVERY_RETRY_DELAY_MS));
        }

        ESP_LOGW(
            TAG,
            "sensor recovery attempt %u/%u",
            (unsigned)(attempt + 1U),
            (unsigned)ACCEL_RECOVERY_MAX_ATTEMPTS);

        /*
         * Software reset restores the LSM6DS3TR-C registers/FIFO
         * to a known state.
         */
        last_error = sensor_reset();

        if (last_error != ESP_OK) {

            ESP_LOGW(
                TAG,
                "sensor reset failed: %s",
                esp_err_to_name(last_error));

            continue;
        }

        /*
         * Reapply all configuration required by MachineGuard.
         */
        last_error = sensor_apply_configuration();

        if (last_error != ESP_OK) {

            ESP_LOGW(
                TAG,
                "sensor configuration failed: %s",
                esp_err_to_name(last_error));

            continue;
        }

        /*
         * Reset the acquisition state one more time after successful
         * reconfiguration. This guarantees that the next published
         * block starts at index zero.
         */
        s_data.index = 0U;

        return ESP_OK;
    }

    return last_error;
}


/* -------------------------------------------------------------------------- */
/* FIFO configuration                                                          */
/* -------------------------------------------------------------------------- */

static esp_err_t fifo_configure(void)
{
    esp_err_t err;

    /*
     * Put FIFO in bypass before reconfiguring it.
     */
    err = write_register(
        LSM6DS3TR_C_FIFO_CTRL5,
        LSM6DS3TR_C_FIFO_MODE_BYPASS);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * FIFO watermark:
     *
     * 64 samples
     * 3 words/sample
     * = 192 words
     */
    err = write_register(
        LSM6DS3TR_C_FIFO_CTRL1,
        (uint8_t)(
            ACCEL_FIFO_WATERMARK_WORDS & 0xFFU));

    if (err != ESP_OK) {
        return err;
    }

    err = write_register(
        LSM6DS3TR_C_FIFO_CTRL2,
        (uint8_t)(
            (ACCEL_FIFO_WATERMARK_WORDS >> 8) &
            0x07U));

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Accelerometer data without decimation.
     */
    err = write_register(
        LSM6DS3TR_C_FIFO_CTRL3,
        LSM6DS3TR_C_DEC_FIFO_XL_NO_DECIMATION);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * No additional FIFO configuration.
     */
    err = write_register(
        LSM6DS3TR_C_FIFO_CTRL4,
        0U);

    if (err != ESP_OK) {
        return err;
    }

    /*
     * FIFO:
     * 6.66 kHz
     * continuous mode
     */
    return write_register(
        LSM6DS3TR_C_FIFO_CTRL5,
        LSM6DS3TR_C_ODR_FIFO_6K66_HZ |
        LSM6DS3TR_C_FIFO_MODE_CONTINUOUS);
}


/* -------------------------------------------------------------------------- */
/* FIFO acquisition                                                            */
/* -------------------------------------------------------------------------- */

static esp_err_t fifo_get_level(
    uint16_t *word_count,
    bool *overrun)
{
    if (word_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status1 = 0U;
    uint8_t status2 = 0U;

    esp_err_t err = read_register(
        LSM6DS3TR_C_FIFO_STATUS1,
        &status1);

    if (err != ESP_OK) {
        return err;
    }

    err = read_register(
        LSM6DS3TR_C_FIFO_STATUS2,
        &status2);

    if (err != ESP_OK) {
        return err;
    }

    const bool overrun_detected =
        (status2 &
         LSM6DS3TR_C_FIFO_STATUS2_OVERRUN) != 0U;

    if (overrun_detected) {
        ESP_LOGW(
            TAG,
            "FIFO overrun; oldest samples were discarded");
    }

    if (overrun != NULL) {
        *overrun = overrun_detected;
    }

    *word_count =
        ((uint16_t)(
            status2 &
            LSM6DS3TR_C_FIFO_STATUS2_DIFF_FIFO_MASK) << 8) |
        status1;

    return ESP_OK;
}


static esp_err_t fifo_read_samples(uint16_t sample_count)
{
    if (sample_count == 0U ||
        sample_count > ACCEL_FIFO_SAMPLES_PER_READ) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t data_bytes =
        sample_count *
        ACCEL_FIFO_WORDS_PER_SAMPLE *
        sizeof(int16_t);

    /*
     * Clear only the portion used by the transaction.
     */
    memset(
        s_fifo_tx,
        0,
        data_bytes + 1U);

    memset(
        s_fifo_rx,
        0,
        data_bytes + 1U);

    s_fifo_tx[0] =
        LSM6DS3TR_C_FIFO_DATA_OUT_L |
        LSM6DS3TR_C_SPI_READ;

    memset(
        &s_fifo_transaction,
        0,
        sizeof(s_fifo_transaction));

    s_fifo_transaction.length =
        (data_bytes + 1U) * 8U;

    s_fifo_transaction.tx_buffer =
        s_fifo_tx;

    s_fifo_transaction.rx_buffer =
        s_fifo_rx;

    if (xSemaphoreTake(
            s_ctx->mutex_spi2,
            ACCEL_SPI_MUTEX_TIMEOUT_TICKS) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = spi_execute_transaction(
        &s_fifo_transaction,
        ACCEL_SPI_PENDING_FIFO);

    xSemaphoreGive(s_ctx->mutex_spi2);

    return err;
}


/* -------------------------------------------------------------------------- */
/* Sample processing                                                           */
/* -------------------------------------------------------------------------- */

static void process_fifo_samples(uint16_t sample_count)
{
    for (uint16_t sample = 0U;
         sample < sample_count;
         ++sample) {

        const size_t offset =
            1U +
            sample *
            ACCEL_FIFO_WORDS_PER_SAMPLE *
            sizeof(int16_t) +
            ACCEL_SELECTED_AXIS *
            sizeof(int16_t);

        const accel_sample_t selected =
            (accel_sample_t)(
                (uint16_t)s_fifo_rx[offset] |
                ((uint16_t)s_fifo_rx[offset + 1U] << 8));

        s_data.write->samples[s_data.index++] =
            selected;

        if (s_data.index == ACCEL_BLOCK_SIZE) {
            publish_completed_buffer();
        }
    }
}


static void publish_completed_buffer(void)
{
    accel_block_t *completed =
        s_data.write;

    /*
     * Swap ping-pong buffers before publishing.
     */
    s_data.write =
        s_data.process;

    s_data.process =
        completed;

    s_data.index = 0U;

    s_data.ping_active =
        (s_data.write == &s_data.ping);

    if (xQueueOverwrite(
            s_ctx->queue_accel_block_to_dsp,
            s_data.process) != pdPASS) {

        ESP_LOGW(
            TAG,
            "accelerometer block queue overwrite failed");
    }
}