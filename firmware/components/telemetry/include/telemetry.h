/**
 * @file telemetry.h
 * @brief Wi-Fi and MQTT telemetry task.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Telemetry FreeRTOS task.
 *
 * Initializes Wi-Fi and MQTT connectivity and periodically publishes
 * the latest machine telemetry to the configured MQTT broker.
 *
 * @param arg Pointer to app_context_t.
 */
void task_telemetry(void *arg);

/**
 * @brief Returns whether Wi-Fi is currently connected.
 *
 * @return true if Wi-Fi is connected, false otherwise.
 */
bool telemetry_is_wifi_connected(void);

/**
 * @brief Returns whether MQTT is currently connected.
 *
 * @return true if MQTT is connected, false otherwise.
 */
bool telemetry_is_mqtt_connected(void);

/**
 * @brief Returns whether the last telemetry publication succeeded.
 *
 * @return true if the last publication was successful, false otherwise.
 */
bool telemetry_get_last_publish_status(void);

/**
 * @brief Returns the timestamp of the last telemetry publication.
 *
 * @return Unix timestamp in milliseconds.
 */
uint64_t telemetry_get_last_publish_timestamp_ms(void);

#ifdef __cplusplus
}
#endif