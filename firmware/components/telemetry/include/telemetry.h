/**
 * @file telemetry.h
 * @brief Wi-Fi and MQTT telemetry task.
 */

#pragma once

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

#ifdef __cplusplus
}
#endif