/**
 * @file sensors.c
 * @brief Implementação da task de coleta de sensores
 */

#include "sensors.h"
#include "app_context.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
/* accel_read_raw may be implemented in the accelerometer component; declare
 * it weakly so linking still succeeds if the driver is omitted during testing.
 */
extern esp_err_t accel_read_raw(accel_sample_t *out) __attribute__((weak));
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensors";

void task_sensors(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    ESP_LOGI(TAG, "task_sensors iniciada no Core 1");
    ESP_LOGI(TAG, "Coleta de dados: acelerômetro + RPM");
    
    while (1) {
        accel_sample_t sample;

        if (accel_read_raw != NULL) {
            if (accel_read_raw(&sample) != ESP_OK) {
                /* read failed, zero sample */
                sample.x = sample.y = sample.z = 0;
            }
        } else {
            /* fallback when accel_read_raw is not linked */
            sample.x = sample.y = sample.z = 0;
        }

        /* Protege acesso ao buffer de amostras */
        if (xSemaphoreTake(ctx->mutex_accel_buffer, pdMS_TO_TICKS(10)) == pdTRUE) {
            /* Escreve na posição head */
            ctx->accel_input.samples[ctx->accel_input.head] = sample;
            ctx->accel_input.head = (ctx->accel_input.head + 1) % ACCEL_WINDOW_SIZE;
            if (ctx->accel_input.count < ACCEL_WINDOW_SIZE) {
                ctx->accel_input.count++;
            }
            xSemaphoreGive(ctx->mutex_accel_buffer);
        }

        // TODO: ler o RPM via PCNT e armazenar em ctx->current_rpm

        //ESP_LOGI("Sensor","Task sensor chamada");
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}