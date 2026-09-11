/**
 * @file sensors.c
 * @brief Temperature and battery sensor task.
 */

#include "sensors.h"

#include "app_context.h"
#include "ds18b20.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_device.h"
#include "onewire_types.h"

static const char *TAG = "sensors";

#define SENSOR_ONEWIRE_GPIO          4
#define SENSOR_UPDATE_PERIOD_MS      1000

static onewire_bus_handle_t s_onewire_bus = NULL;
static ds18b20_device_handle_t s_ds18b20 = NULL;

static esp_err_t init_ds18b20(void);
static esp_err_t read_temperature(float *temperature_c);

void task_sensors(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL || ctx->queue_sensors_to_system == NULL) {
        ESP_LOGE(TAG, "invalid sensors context");
        vTaskDelete(NULL);
        return;
    }

    if (init_ds18b20() != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize DS18B20");

        sensor_result_t result = {
            .temperature_c = 0.0f,
            .temperature_valid = false
        };

        while (true) {
            xQueueOverwrite(
                ctx->queue_sensors_to_system,
                &result
            );

            vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_PERIOD_MS));
        }
    }

    ESP_LOGI(TAG, "task_sensors started");

    while (true) {
        sensor_result_t result = {
            .temperature_c = 0.0f,
            .temperature_valid = false
        };

        esp_err_t ret = read_temperature(&result.temperature_c);

        if (ret == ESP_OK) {
            result.temperature_valid = true;

            ESP_LOGI(
                TAG,
                "temperature: %.2f C",
                result.temperature_c
            );
        } else {
            ESP_LOGW(
                TAG,
                "failed to read DS18B20: %s",
                esp_err_to_name(ret)
            );
        }

        if (xQueueOverwrite(
                ctx->queue_sensors_to_system,
                &result) != pdPASS) {

            ESP_LOGW(
                TAG,
                "failed to publish sensor result"
            );
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_PERIOD_MS));
    }
}

static esp_err_t init_ds18b20(void)
{
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = SENSOR_ONEWIRE_GPIO,
        .flags = {
            .en_pull_up = false
        }
    };

    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10
    };

    esp_err_t ret = onewire_new_bus_rmt(
        &bus_config,
        &rmt_config,
        &s_onewire_bus
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "failed to create 1-Wire bus: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    ESP_LOGI(
        TAG,
        "1-Wire bus initialized on GPIO%d",
        SENSOR_ONEWIRE_GPIO
    );

    onewire_device_iter_handle_t iter = NULL;

    ret = onewire_new_device_iter(
        s_onewire_bus,
        &iter
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "failed to create 1-Wire device iterator: %s",
            esp_err_to_name(ret)
        );

        onewire_bus_del(s_onewire_bus);
        s_onewire_bus = NULL;

        return ret;
    }

    while (true) {
        onewire_device_t device;

        ret = onewire_device_iter_get_next(
            iter,
            &device
        );

        if (ret == ESP_ERR_NOT_FOUND) {
            break;
        }

        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "failed during 1-Wire device enumeration: %s",
                esp_err_to_name(ret)
            );

            break;
        }

        ds18b20_config_t ds18b20_config;

        ret = ds18b20_new_device_from_enumeration(
            &device,
            &ds18b20_config,
            &s_ds18b20
        );

        if (ret == ESP_OK) {
            onewire_device_address_t address;

            ret = ds18b20_get_device_address(
                s_ds18b20,
                &address
            );

            if (ret != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "failed to read DS18B20 address: %s",
                    esp_err_to_name(ret)
                );

                ds18b20_del_device(s_ds18b20);
                s_ds18b20 = NULL;
                break;
            }

            ESP_LOGI(
                TAG,
                "DS18B20 found: %016llX",
                (unsigned long long)address
            );

            break;
        }

        ESP_LOGW(
            TAG,
            "1-Wire device is not a DS18B20: %s",
            esp_err_to_name(ret)
        );
    }

    esp_err_t iter_ret = onewire_del_device_iter(iter);

    if (iter_ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "failed to delete 1-Wire device iterator: %s",
            esp_err_to_name(iter_ret)
        );
    }

    if (s_ds18b20 == NULL) {
        ESP_LOGE(TAG, "no DS18B20 device found");

        onewire_bus_del(s_onewire_bus);
        s_onewire_bus = NULL;

        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

static esp_err_t read_temperature(float *temperature_c)
{
    if (temperature_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_onewire_bus == NULL || s_ds18b20 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret =
        ds18b20_trigger_temperature_conversion_for_all(
            s_onewire_bus
        );

    if (ret != ESP_OK) {
        return ret;
    }

    return ds18b20_get_temperature(
        s_ds18b20,
        temperature_c
    );
}