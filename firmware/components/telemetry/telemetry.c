/**
 * @file telemetry.c
 * @brief Wi-Fi and MQTT telemetry implementation.
 */

#include "telemetry.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_timeval.h>
#include <sys/time.h>

#include "app_context.h"

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"

#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mqtt_client.h"

#include "nvs_flash.h"
#include "portmacro.h"

/* ============================================================================
 * Private constants and macros
 * ========================================================================== */

static const char *TAG = "telemetry";

/**
 * @brief Wi-Fi network credentials.
 *
 * Replace these values with the network used during testing.
 */
#define TELEMETRY_WIFI_SSID              "Marcia"
#define TELEMETRY_WIFI_PASSWORD          "jpgmoveis10"

/**
 * @brief ThingsBoard MQTT configuration.
 *
 * The access token is used as the MQTT username.
 */
#define TELEMETRY_MQTT_HOST              "mqtt.thingsboard.cloud"
#define TELEMETRY_MQTT_PORT              8883
#define TELEMETRY_MQTT_ACCESS_TOKEN      "vPHGofc5969UgllaHPXI"
#define TELEMETRY_MQTT_TOPIC             "v1/devices/me/telemetry"

/**
 * @brief Telemetry publication period.
 */
#define TELEMETRY_PUBLISH_PERIOD_MS      (60000U)

/* ============================================================================
 * Private variables
 * ========================================================================== */

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static bool s_wifi_connected = false;
static bool s_mqtt_connected = false;

static bool s_last_publish_ok = false;
static uint64_t s_last_publish_timestamp_ms = 0U;

/* ============================================================================
 * Private function prototypes
 * ========================================================================== */

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
);

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
);

static esp_err_t wifi_init(void);

static esp_err_t mqtt_init(void);

static esp_err_t publish_telemetry( const telemetry_data_t *data);

static const char *state_to_string(system_state_t state);


bool telemetry_is_wifi_connected(void);

bool telemetry_is_mqtt_connected(void);

bool telemetry_get_last_publish_status(void);

uint64_t telemetry_get_last_publish_timestamp_ms(void);


/* ============================================================================
 * Public function implementations
 * ========================================================================== */

void task_telemetry(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL ||
        ctx->queue_system_to_telemetry == NULL) {

        ESP_LOGE(
            TAG,
            "invalid telemetry context"
        );

        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(
        TAG,
        "telemetry task started"
    );

    if (wifi_init() != ESP_OK) {

        ESP_LOGE(
            TAG,
            "failed to initialize Wi-Fi"
        );

        vTaskDelete(NULL);
        return;
    }

    if (mqtt_init() != ESP_OK) {

        ESP_LOGE(
            TAG,
            "failed to initialize MQTT"
        );

        vTaskDelete(NULL);
        return;
    }

    telemetry_data_t latest_data = {0};

    bool has_data = false;

    TickType_t last_publish_time = xTaskGetTickCount();

    while (true) {

        telemetry_data_t received_data;

        if (xQueueReceive(
                ctx->queue_system_to_telemetry,
                &received_data,
                pdMS_TO_TICKS(100)) == pdTRUE) {

            latest_data = received_data;
            has_data = true;
        }

        /*
         * Publish periodically.
         */
        const TickType_t now = xTaskGetTickCount();

        if ((now - last_publish_time) >=
            pdMS_TO_TICKS(TELEMETRY_PUBLISH_PERIOD_MS)) {

            last_publish_time = now;

            if (!has_data) {

                ESP_LOGD(
                    TAG,
                    "no telemetry data available yet"
                );

                continue;
            }

            if (!s_wifi_connected) {

                ESP_LOGW(
                    TAG,
                    "Wi-Fi not connected, telemetry skipped"
                );

                continue;
            }

            if (!s_mqtt_connected) {

                ESP_LOGW(
                    TAG,
                    "MQTT not connected, telemetry skipped"
                );

                continue;
            }

            const esp_err_t err =
                publish_telemetry(&latest_data);

            if (err != ESP_OK) {

                ESP_LOGW(
                    TAG,
                    "failed to publish telemetry: %s",
                    esp_err_to_name(err)
                );
            }
        }
    }
}

/* ============================================================================
 * Private function implementations
 * ========================================================================== */

static esp_err_t wifi_init(void)
{
    esp_err_t err;

    err = esp_netif_init();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "esp_netif_init failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_event_loop_create_default();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "esp_event_loop_create_default failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_config =
        WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&wifi_config);

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "esp_wifi_init failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL
    );

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "failed to register Wi-Fi event handler: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL
    );

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "failed to register IP event handler: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    wifi_config_t sta_config = {
        .sta = {
            .ssid = TELEMETRY_WIFI_SSID,
            .password = TELEMETRY_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "esp_wifi_set_mode failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_wifi_set_config(
        WIFI_IF_STA,
        &sta_config
    );

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "esp_wifi_set_config failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_wifi_start();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "esp_wifi_start failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(
        TAG,
        "Wi-Fi initialization completed"
    );

    return ESP_OK;
}

static esp_err_t mqtt_init(void)
{
    if (s_mqtt_client != NULL) {
        return ESP_OK;
    }

    const esp_mqtt_client_config_t mqtt_config = {
        .broker = {
            .address = {
                .uri = "mqtts://" TELEMETRY_MQTT_HOST,
                .port = TELEMETRY_MQTT_PORT,
            },
            .verification = {
                .crt_bundle_attach =
                    esp_crt_bundle_attach,
            },
        },

        .credentials = {
            .username =
                TELEMETRY_MQTT_ACCESS_TOKEN,
        },
    };

    s_mqtt_client =
        esp_mqtt_client_init(&mqtt_config);

    if (s_mqtt_client == NULL) {

        ESP_LOGE(
            TAG,
            "esp_mqtt_client_init failed"
        );

        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        s_mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "failed to register MQTT event handler: %s",
            esp_err_to_name(err)
        );

        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;

        return err;
    }

    err = esp_mqtt_client_start(s_mqtt_client);

    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "esp_mqtt_client_start failed: %s",
            esp_err_to_name(err)
        );

        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;

        return err;
    }

    ESP_LOGI(
        TAG,
        "MQTT initialization completed"
    );

    return ESP_OK;
}

static esp_err_t publish_telemetry(
    const telemetry_data_t *data
)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mqtt_client == NULL ||
        !s_mqtt_connected) {

        return ESP_ERR_INVALID_STATE;
    }

    char payload[1024];

    const int length = snprintf(
        payload,
        sizeof(payload),

        "{"
            "\"state\":\"%s\","
            "\"rms\":%.6f,"
            "\"kurtosis\":%.6f,"
            "\"crestFactor\":%.6f,"
            "\"bin1xRpmAmplitude\":%.6f,"
            "\"frequencyHz\":%.3f,"
            "\"rpm\":%.1f,"
            "\"temperatureC\":%.2f,"
            "\"temperatureValid\":%s,"
            "\"rmsZscore\":%.3f,"
            "\"kurtosisZscore\":%.3f,"
            "\"bin1xRpmZscore\":%.3f,"
            "\"rmsAbnormal\":%s,"
            "\"kurtosisAbnormal\":%s,"
            "\"bin1xRpmAbnormal\":%s"
        "}",

        state_to_string(data->state),

        data->rms,
        data->kurtosis,
        data->crest_factor,

        data->bin_1xrpm_amplitude,
        data->frequency_hz,
        data->rpm,

        data->temperature_c,
        data->temperature_valid ? "true" : "false",

        data->rms_zscore,
        data->kurtosis_zscore,
        data->bin_1xrpm_zscore,

        data->rms_abnormal ? "true" : "false",
        data->kurtosis_abnormal ? "true" : "false",
        data->bin_1xrpm_abnormal ? "true" : "false"
    );

    if (length < 0) {

        ESP_LOGE(TAG, "failed to format telemetry payload");

        return ESP_FAIL;
    }

    if ((size_t)length >= sizeof(payload)) {

        ESP_LOGE(
            TAG,
            "telemetry payload buffer is too small"
        );

        return ESP_ERR_NO_MEM;
    }

    const int message_id =
        esp_mqtt_client_publish(
            s_mqtt_client,
            TELEMETRY_MQTT_TOPIC,
            payload,
            length,
            1,
            0
        );

    if (message_id < 0) {

        ESP_LOGE(
            TAG,
            "MQTT publish failed"
        );

        s_last_publish_ok = false;

        return ESP_FAIL;
    }

    s_last_publish_ok = true;

    struct timeval tv;

    if (gettimeofday(&tv, NULL) == 0) {

        s_last_publish_timestamp_ms =
            ((uint64_t)tv.tv_sec * 1000ULL) +
            ((uint64_t)tv.tv_usec / 1000ULL);
    }

    ESP_LOGI(
        TAG,
        "telemetry published | message_id=%d",
        message_id
    );

    ESP_LOGD(
        TAG,
        "payload: %s",
        payload
    );

    return ESP_OK;
}

static const char *state_to_string(
    system_state_t state
)
{
    switch (state) {

        case SYSTEM_STATE_INIT:
            return "INIT";

        case SYSTEM_STATE_WARMUP:
            return "WARMUP";

        case SYSTEM_STATE_HEALTHY:
            return "HEALTHY";

        case SYSTEM_STATE_ALARM:
            return "ALARM";

        default:
            return "UNKNOWN";
    }
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT) {

        switch (event_id) {

            case WIFI_EVENT_STA_START:

                ESP_LOGI(
                    TAG,
                    "Wi-Fi station started"
                );

                esp_wifi_connect();

                break;

            case WIFI_EVENT_STA_DISCONNECTED:

                s_wifi_connected = false;
                s_mqtt_connected = false;

                ESP_LOGW(
                    TAG,
                    "Wi-Fi disconnected, reconnecting"
                );

                esp_wifi_connect();

                break;

            default:
                break;
        }

        return;
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {

        s_wifi_connected = true;

        ESP_LOGI(
            TAG,
            "Wi-Fi connected and IP acquired"
        );
    }
}

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:

            s_mqtt_connected = true;

            ESP_LOGI(
                TAG,
                "MQTT connected"
            );

            break;

        case MQTT_EVENT_DISCONNECTED:

            s_mqtt_connected = false;

            ESP_LOGW(
                TAG,
                "MQTT disconnected"
            );

            break;

        case MQTT_EVENT_ERROR:

            s_mqtt_connected = false;

            ESP_LOGE(
                TAG,
                "MQTT error"
            );

            break;

        default:
            break;
    }
}


bool telemetry_is_wifi_connected(void)
{
    return s_wifi_connected;
}

bool telemetry_is_mqtt_connected(void)
{
    return s_mqtt_connected;
}

bool telemetry_get_last_publish_status(void)
{
    return s_last_publish_ok;
}

uint64_t telemetry_get_last_publish_timestamp_ms(void)
{
    return s_last_publish_timestamp_ms;
}