/**
 * @file system.c
 * @brief System decision task implementation.
 */

#include "system.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "app_context.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include "telemetry.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "portmacro.h"

/* ============================================================================
 * Private constants and macros
 * ========================================================================== */

static const char *TAG = "system";

/**
 * @brief Minimum standard deviation considered valid for Z-score calculation.
 *
 * This prevents division by a value that is effectively zero.
 */
#define SYSTEM_MIN_STDDEV (1.0e-9f)

/**
 * @brief Minimum number of valid 1xRPM observations required
 *        to accept the spectral baseline.
 *
 * This is an initial engineering criterion and must be evaluated
 * with real motor data.
 */
#define SYSTEM_MIN_BIN_VALID_EVALUATIONS (100U)

/**
 * @brief Number of consecutive abnormal evaluations required to enter ALARM.
 */
#define SYSTEM_ALARM_CONSECUTIVE_COUNT  (5U)

/**
 * @brief Number of consecutive normal evaluations required to return HEALTHY.
 */
#define SYSTEM_HEALTHY_CONSECUTIVE_COUNT (5U)

/**
 * @brief Number of consecutive invalid vibration evaluations required
 *        to consider that no motor is connected.
 *
 * An evaluation is considered invalid when DSP reports peak_valid=false.
 */
#define SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT (15U)

/**
 * @brief Initial Z-score threshold.
 */
#define SYSTEM_ZSCORE_THRESHOLD         (3.0f)

#define SYSTEM_BUZZER_GPIO              GPIO_NUM_16
#define SYSTEM_BUZZER_ON_TIME_MS        200U
#define SYSTEM_BUZZER_OFF_TIME_MS       200U
#define SYSTEM_BUZZER_PAUSE_TIME_MS     800U


/* ============================================================================
 * Private types
 * ========================================================================== */

/**
 * @brief Online statistics used during baseline acquisition.
 *
 * Welford's algorithm is used to calculate mean and variance incrementally.
 */
typedef struct
{
    uint32_t count;

    float mean;

    float m2;

} system_online_stats_t;

/**
 * @brief Baseline statistics for one DSP feature.
 */
typedef struct
{
    float mean;
    float stddev;

    bool valid;

} system_baseline_feature_t;

/**
 * @brief Complete healthy baseline.
 */
typedef struct
{
    system_baseline_feature_t rms;
    system_baseline_feature_t kurtosis;
    system_baseline_feature_t bin_1xrpm_amplitude;

    bool valid;

} system_baseline_t;

typedef struct
{
    system_state_t state;

    uint16_t warmup_count;
    uint16_t bin_valid_count;

    system_online_stats_t rms_stats;
    system_online_stats_t kurtosis_stats;
    system_online_stats_t bin_stats;

    system_baseline_t baseline;

    sensor_result_t latest_sensor;
    system_diagnostics_t diagnostics;

    uint8_t consecutive_abnormal;
    uint8_t consecutive_normal;
    uint8_t consecutive_no_motor;

} system_context_t;

/* ============================================================================
 * Private variables
 * ========================================================================== */

static system_context_t s_system;

static TaskHandle_t s_buzzer_task = NULL;

/* ============================================================================
 * Private function prototypes
 * ========================================================================== */

static void reset_context(void);

static void process_warmup(const dsp_result_t *result);

static void finalize_baseline(void);

static void update_online_stats(system_online_stats_t *stats,
                                float value);

static system_baseline_feature_t
build_baseline_feature(const system_online_stats_t *stats);

static float calculate_zscore(float value,
                              const system_baseline_feature_t *baseline);

static bool evaluate_feature(float value,
                             const system_baseline_feature_t *baseline);

static bool evaluate_abnormality(const dsp_result_t *result);

static void process_monitoring(const dsp_result_t *result);

static void process_pending_commands(app_context_t *ctx);

static void process_pending_sensor_results(app_context_t *ctx);

static void publish_hmi_data(app_context_t *ctx,
                             const dsp_result_t *result);


static void publish_telemetry_data(app_context_t *ctx,
                                   const dsp_result_t *result);
                             
static void log_baseline(void);

static const char *state_to_string(system_state_t state);

static void reset_monitoring_context(void);

static bool process_motor_presence(const dsp_result_t *result);

static void buzzer_init(void);
static void task_buzzer(void *arg);
static void buzzer_start(void);
static void buzzer_stop(void);

/* ============================================================================
 * Public function implementations
 * ========================================================================== */

void task_system(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL ||
        ctx->queue_dsp_to_system == NULL ||
        ctx->queue_sensors_to_system == NULL ||
        ctx->queue_hmi_to_system == NULL ||
        ctx->queue_system_to_hmi == NULL) {

        ESP_LOGE(TAG, "invalid system context");
        vTaskDelete(NULL);
        return;
    }

    reset_context();

    buzzer_init();

    ESP_LOGI(
        TAG,
        "task started | warm-up: %u evaluations | threshold: %.2f",
        SYSTEM_WARMUP_EVALUATIONS,
        SYSTEM_ZSCORE_THRESHOLD
    );

    while (true) {

        static dsp_result_t result;

        if (xQueueReceive(
                ctx->queue_dsp_to_system,
                &result,
                portMAX_DELAY) != pdTRUE) {

            continue;
        }

        process_pending_commands(ctx);
        process_pending_sensor_results(ctx);

        /*
         * NO_MOTOR has special handling.
         *
         * While the system is already in NO_MOTOR, remain there until
         * a valid vibration peak is detected again.
         */
        if (s_system.state == SYSTEM_STATE_NO_MOTOR) {

            if (!result.peak_valid) {

                publish_hmi_data(ctx, &result);
                publish_telemetry_data(ctx, &result);

                continue;
            }

            /*
             * Motor vibration detected again.
             *
             * If a validated baseline already exists, return directly
             * to monitoring. Otherwise start a new warm-up.
             */
            s_system.consecutive_no_motor = 0U;

            if (s_system.baseline.valid) {

                s_system.state = SYSTEM_STATE_HEALTHY;

                s_system.consecutive_abnormal = 0U;
                s_system.consecutive_normal = 0U;

                ESP_LOGI(
                    TAG,
                    "valid vibration detected, resuming monitoring"
                );

                ESP_LOGI(
                    TAG,
                    "state: %s",
                    state_to_string(s_system.state)
                );

                process_monitoring(&result);

            } else {

                s_system.state = SYSTEM_STATE_WARMUP;

                /*
                 * Start a fresh baseline acquisition.
                 */
                s_system.warmup_count = 0U;
                s_system.bin_valid_count = 0U;

                s_system.rms_stats =
                    (system_online_stats_t){0};

                s_system.kurtosis_stats =
                    (system_online_stats_t){0};

                s_system.bin_stats =
                    (system_online_stats_t){0};

                s_system.consecutive_abnormal = 0U;
                s_system.consecutive_normal = 0U;

                ESP_LOGI(
                    TAG,
                    "valid vibration detected, starting warm-up"
                );

                ESP_LOGI(
                    TAG,
                    "state: %s",
                    state_to_string(s_system.state)
                );

                process_warmup(&result);
            }

            publish_hmi_data(ctx, &result);
            publish_telemetry_data(ctx, &result);

            continue;
        }

        /*
         * Detect motor absence.
         *
         * A valid peak resets the no-motor counter.
         * Fifteen consecutive invalid evaluations move the
         * system to NO_MOTOR.
         */
        process_motor_presence(&result);

        /*
         * The current result may have caused a transition to NO_MOTOR.
         * Do not process it as WARMUP/HEALTHY/ALARM anymore.
         */
        if (s_system.state == SYSTEM_STATE_NO_MOTOR) {

            publish_hmi_data(ctx, &result);
            publish_telemetry_data(ctx, &result);

            continue;
        }

        switch (s_system.state) {

            case SYSTEM_STATE_INIT:

                s_system.state = SYSTEM_STATE_WARMUP;

                ESP_LOGI(
                    TAG,
                    "state: %s",
                    state_to_string(s_system.state)
                );

                process_warmup(&result);

                break;

            case SYSTEM_STATE_WARMUP:

                process_warmup(&result);

                break;

            case SYSTEM_STATE_HEALTHY:

            case SYSTEM_STATE_ALARM:

                process_monitoring(&result);

                break;

            case SYSTEM_STATE_NO_MOTOR:

                /*
                 * NO_MOTOR is handled before the state switch.
                 */
                break;

            default:

                ESP_LOGE(
                    TAG,
                    "invalid system state: %d",
                    s_system.state
                );

                reset_context();

                break;
        }

        publish_hmi_data(ctx, &result);
        publish_telemetry_data(ctx, &result);
    }
}

/* ============================================================================
 * Private function implementations
 * ========================================================================== */

static void reset_context(void)
{
    s_system.state = SYSTEM_STATE_INIT;

    s_system.warmup_count = 0U;
    s_system.bin_valid_count = 0U;

    s_system.rms_stats = (system_online_stats_t){0};
    s_system.kurtosis_stats = (system_online_stats_t){0};
    s_system.bin_stats = (system_online_stats_t){0};

    s_system.baseline = (system_baseline_t){0};
    s_system.diagnostics = (system_diagnostics_t){0};

    s_system.consecutive_abnormal = 0U;
    s_system.consecutive_normal = 0U;
}

static void process_warmup(const dsp_result_t *result)
{
    if (result == NULL) {
        return;
    }

    /*
     * Only valid vibration evaluations are allowed to contribute
     * to the healthy baseline.
     */
    if (!result->peak_valid) {

        ESP_LOGD(
            TAG,
            "warm-up evaluation ignored: invalid vibration"
        );

        return;
    }

    update_online_stats(
        &s_system.rms_stats,
        result->rms
    );

    update_online_stats(
        &s_system.kurtosis_stats,
        result->kurtosis
    );

    update_online_stats(
        &s_system.bin_stats,
        result->bin_1xrpm_amplitude
    );

    s_system.bin_valid_count++;
    s_system.warmup_count++;

    if ((s_system.warmup_count % 50U) == 0U ||
        s_system.warmup_count == SYSTEM_WARMUP_EVALUATIONS) {

        ESP_LOGI(
            TAG,
            "warm-up: %u/%u | 1xRPM valid: %u",
            s_system.warmup_count,
            SYSTEM_WARMUP_EVALUATIONS,
            s_system.bin_valid_count
        );
    }

    if (s_system.warmup_count >= SYSTEM_WARMUP_EVALUATIONS) {

        finalize_baseline();

        if (s_system.baseline.valid) {

            s_system.state = SYSTEM_STATE_HEALTHY;

            s_system.consecutive_abnormal = 0U;
            s_system.consecutive_normal = 0U;

            ESP_LOGI(TAG, "warm-up completed");

            log_baseline();

            ESP_LOGI(
                TAG,
                "state: %s",
                state_to_string(s_system.state)
            );

        } else {

            ESP_LOGE(
                TAG,
                "warm-up completed but baseline is invalid"
            );

            /*
             * The baseline acquisition failed.
             *
             * Start a completely new 600-valid-evaluation
             * acquisition window.
             */
            s_system.warmup_count = 0U;
            s_system.bin_valid_count = 0U;

            s_system.rms_stats =
                (system_online_stats_t){0};

            s_system.kurtosis_stats =
                (system_online_stats_t){0};

            s_system.bin_stats =
                (system_online_stats_t){0};

            s_system.baseline =
                (system_baseline_t){0};

            s_system.state = SYSTEM_STATE_WARMUP;

            ESP_LOGW(
                TAG,
                "baseline invalid, restarting warm-up"
            );
        }
    }
}

static void finalize_baseline(void)
{
    s_system.baseline.rms =
        build_baseline_feature(&s_system.rms_stats);

    s_system.baseline.kurtosis =
        build_baseline_feature(&s_system.kurtosis_stats);

    s_system.baseline.bin_1xrpm_amplitude =
        build_baseline_feature(&s_system.bin_stats);

    const bool bin_count_valid =
        s_system.bin_valid_count >=
        SYSTEM_MIN_BIN_VALID_EVALUATIONS;

    s_system.baseline.valid =
        s_system.baseline.rms.valid &&
        s_system.baseline.kurtosis.valid &&
        s_system.baseline.bin_1xrpm_amplitude.valid &&
        bin_count_valid;

    ESP_LOGI(
        TAG,
        "baseline validity: RMS=%d | Kurtosis=%d | "
        "1xRPM=%d (%u/%u) | overall=%d",
        s_system.baseline.rms.valid,
        s_system.baseline.kurtosis.valid,
        s_system.baseline.bin_1xrpm_amplitude.valid,
        s_system.bin_valid_count,
        SYSTEM_MIN_BIN_VALID_EVALUATIONS,
        s_system.baseline.valid
    );
}

static void update_online_stats(system_online_stats_t *stats,
                                float value)
{
    if (stats == NULL) {
        return;
    }

    stats->count++;

    const float delta =
        value - stats->mean;

    stats->mean +=
        delta / (float)stats->count;

    const float delta2 =
        value - stats->mean;

    stats->m2 +=
        delta * delta2;
}

static system_baseline_feature_t
build_baseline_feature(const system_online_stats_t *stats)
{
    system_baseline_feature_t feature = {
        .mean = 0.0f,
        .stddev = 0.0f,
        .valid = false
    };

    if (stats == NULL || stats->count < 2U) {
        return feature;
    }

    /*
     * Population variance is used because the 600 warm-up evaluations
     * represent the complete baseline dataset for this machine session.
     */
    const float variance =
        stats->m2 / (float)stats->count;

    feature.mean = stats->mean;

    feature.stddev =
        sqrtf(fmaxf(variance, 0.0f));

    feature.valid =
        isfinite(feature.mean) &&
        isfinite(feature.stddev) &&
        (feature.stddev > SYSTEM_MIN_STDDEV);

    return feature;
}

static float calculate_zscore(
    float value,
    const system_baseline_feature_t *baseline)
{
    if (baseline == NULL || !baseline->valid) {
        return 0.0f;
    }

    return (value - baseline->mean) /
           baseline->stddev;
}
static bool evaluate_feature(
    float value,
    const system_baseline_feature_t *baseline)
{
    if (baseline == NULL || !baseline->valid) {
        return false;
    }

    const float zscore =
        calculate_zscore(value, baseline);

    /*
     * A feature is considered abnormal only when both conditions
     * are satisfied:
     *
     * 1. The value exceeds the healthy baseline mean by the
     *    configured fixed margin.
     *
     * 2. The value exceeds the configured Z-score threshold.
     *
     * This prevents small statistical deviations from being
     * considered abnormal merely because they exceed the fixed
     * percentage margin.
     */
    const float baseline_threshold =
        baseline->mean *
        (1.0f + SYSTEM_BASELINE_MARGIN);

    const bool exceeds_baseline_margin =
        value > baseline_threshold;

    const bool exceeds_zscore_threshold =
        zscore > SYSTEM_ZSCORE_THRESHOLD;

    return exceeds_baseline_margin &&
           exceeds_zscore_threshold;
}


static bool evaluate_abnormality(const dsp_result_t *result)
{
    s_system.diagnostics = (system_diagnostics_t){0};

    if (result == NULL || !s_system.baseline.valid) {
        return false;
    }

    uint8_t abnormal_features = 0U;

    s_system.diagnostics.rms_zscore =
        calculate_zscore(result->rms, &s_system.baseline.rms);
    s_system.diagnostics.rms_abnormal =
        evaluate_feature(result->rms, &s_system.baseline.rms);
    if (s_system.diagnostics.rms_abnormal) {

        abnormal_features++;
    }

    s_system.diagnostics.kurtosis_zscore =
        calculate_zscore(result->kurtosis, &s_system.baseline.kurtosis);
    s_system.diagnostics.kurtosis_abnormal =
        evaluate_feature(result->kurtosis, &s_system.baseline.kurtosis);
    if (s_system.diagnostics.kurtosis_abnormal) {

        abnormal_features++;
    }

    s_system.diagnostics.bin_1xrpm_zscore =
        calculate_zscore(result->bin_1xrpm_amplitude,
                         &s_system.baseline.bin_1xrpm_amplitude);
    s_system.diagnostics.bin_1xrpm_abnormal =
        evaluate_feature(result->bin_1xrpm_amplitude,
                         &s_system.baseline.bin_1xrpm_amplitude);
    if (s_system.diagnostics.bin_1xrpm_abnormal) {

        abnormal_features++;
    }

    /*
     * Two out of three features must be abnormal for the complete
     * evaluation to be considered abnormal.
     */
    return abnormal_features >= 2U;
}

static void process_monitoring(const dsp_result_t *result)
{
    if (result == NULL || !s_system.baseline.valid) {
        return;
    }

    const bool abnormal =
        evaluate_abnormality(result);

    if (s_system.state == SYSTEM_STATE_HEALTHY) {

        if (abnormal) {

            s_system.consecutive_abnormal++;
            s_system.consecutive_normal = 0U;

            ESP_LOGD(
                TAG,
                "abnormal evaluation: %u/%u",
                s_system.consecutive_abnormal,
                SYSTEM_ALARM_CONSECUTIVE_COUNT
            );

            if (s_system.consecutive_abnormal >=
                SYSTEM_ALARM_CONSECUTIVE_COUNT) {

                s_system.state = SYSTEM_STATE_ALARM;

                s_system.consecutive_abnormal = 0U;
                s_system.consecutive_normal = 0U;


                ESP_LOGW(
                    TAG,
                    "state changed: %s",
                    state_to_string(s_system.state)
                );
            }
        }
        else {

            /*
             * A normal evaluation breaks the consecutive abnormal
             * sequence. No counter is required while the system
             * is already healthy.
             */
            s_system.consecutive_abnormal = 0U;
        }

    }
    else if (s_system.state == SYSTEM_STATE_ALARM) {

        if (abnormal) {

            /*
             * An abnormal evaluation breaks the consecutive normal
             * sequence. The system remains in ALARM.
             */
            s_system.consecutive_normal = 0U;
        }
        else {

            s_system.consecutive_normal++;

            ESP_LOGD(
                TAG,
                "normal evaluation: %u/%u",
                s_system.consecutive_normal,
                SYSTEM_HEALTHY_CONSECUTIVE_COUNT
            );

            if (s_system.consecutive_normal >=
                SYSTEM_HEALTHY_CONSECUTIVE_COUNT) {

                s_system.state = SYSTEM_STATE_HEALTHY;

                s_system.consecutive_normal = 0U;
                s_system.consecutive_abnormal = 0U;

                ESP_LOGI(
                    TAG,
                    "state changed: %s",
                    state_to_string(s_system.state)
                );
            }
        }
    }
}

static void process_pending_commands(app_context_t *ctx)
{
    system_command_t command;

    while (xQueueReceive(ctx->queue_hmi_to_system, &command, 0) == pdTRUE) {
        if (command == SYSTEM_COMMAND_RESET_WARMUP) {
            reset_context();
            ESP_LOGI(TAG, "warm-up reset requested by HMI");
        } else {
            ESP_LOGW(TAG, "unknown system command: %d", command);
        }
    }
}

static void process_pending_sensor_results(app_context_t *ctx)
{
    sensor_result_t result;

    while (xQueueReceive(ctx->queue_sensors_to_system, &result, 0) == pdTRUE) {
        s_system.latest_sensor = result;
    }
}

static void publish_hmi_data(app_context_t *ctx,
                             const dsp_result_t *result)
{
    hmi_data_t data = {0};

    if (ctx == NULL || result == NULL) {
        return;
    }


    data.telemetry.wifi_connected = telemetry_is_wifi_connected();

    data.telemetry.mqtt_connected = telemetry_is_mqtt_connected();

    data.telemetry.last_publish_ok = telemetry_get_last_publish_status();

    data.telemetry.last_publish_timestamp_ms = telemetry_get_last_publish_timestamp_ms();

    data.state.state = s_system.state;

    data.features.rms = result->rms;
    data.features.kurtosis = result->kurtosis;
    data.features.crest_factor = result->crest_factor;
    data.features.bin_1xrpm_amplitude = result->bin_1xrpm_amplitude;
    data.features.frequency_hz = result->frequency_hz;
    data.features.rpm = result->rpm;
    data.features.temperature_c = s_system.latest_sensor.temperature_c;
    data.features.temperature_valid = s_system.latest_sensor.temperature_valid;

    data.diagnostics = s_system.diagnostics;
    data.warmup.evaluations = s_system.warmup_count;
    data.warmup.required = SYSTEM_WARMUP_EVALUATIONS;
    data.warmup.bin_1xrpm_valid = s_system.bin_valid_count;

    memcpy(data.fft_magnitude,
           &result->magnitude[HMI_FFT_FIRST_BIN],
           sizeof(data.fft_magnitude));

    if (xQueueOverwrite(ctx->queue_system_to_hmi, &data) != pdPASS) {
        ESP_LOGW(TAG, "HMI data queue overwrite failed");
    }
}

static void log_baseline(void)
{
    ESP_LOGI(
        TAG,
        "baseline RMS: mean=%.6f stddev=%.6f valid=%d",
        s_system.baseline.rms.mean,
        s_system.baseline.rms.stddev,
        s_system.baseline.rms.valid
    );

    ESP_LOGI(
        TAG,
        "baseline Kurtosis: mean=%.6f stddev=%.6f valid=%d",
        s_system.baseline.kurtosis.mean,
        s_system.baseline.kurtosis.stddev,
        s_system.baseline.kurtosis.valid
    );

    ESP_LOGI(
        TAG,
        "baseline 1xRPM amplitude: mean=%.6f stddev=%.6f valid=%d",
        s_system.baseline.bin_1xrpm_amplitude.mean,
        s_system.baseline.bin_1xrpm_amplitude.stddev,
        s_system.baseline.bin_1xrpm_amplitude.valid
    );
}

static void publish_telemetry_data(app_context_t *ctx,
                                   const dsp_result_t *result)
{
    telemetry_data_t data = {0};

    if (ctx == NULL || result == NULL) {
        return;
    }

    data.state = s_system.state;

    data.rms = result->rms;
    data.kurtosis = result->kurtosis;
    data.crest_factor = result->crest_factor;

    data.bin_1xrpm_amplitude = result->bin_1xrpm_amplitude;
    data.frequency_hz = result->frequency_hz;
    data.rpm = result->rpm;

    data.temperature_c =
        s_system.latest_sensor.temperature_c;

    data.temperature_valid =
        s_system.latest_sensor.temperature_valid;

    data.rms_zscore =
        s_system.diagnostics.rms_zscore;

    data.kurtosis_zscore =
        s_system.diagnostics.kurtosis_zscore;

    data.bin_1xrpm_zscore =
        s_system.diagnostics.bin_1xrpm_zscore;

    data.rms_abnormal =
        s_system.diagnostics.rms_abnormal;

    data.kurtosis_abnormal =
        s_system.diagnostics.kurtosis_abnormal;

    data.bin_1xrpm_abnormal =
        s_system.diagnostics.bin_1xrpm_abnormal;

    if (xQueueOverwrite(
            ctx->queue_system_to_telemetry,
            &data) != pdPASS) {

        ESP_LOGW(
            TAG,
            "Telemetry data queue overwrite failed"
        );
    }
}

static const char *state_to_string(system_state_t state)
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

        case SYSTEM_STATE_NO_MOTOR:
            return "NO_MOTOR";

        default:
            return "UNKNOWN";
    }
}


static void reset_monitoring_context(void)
{
    /*
     * Preserve the validated baseline.
     *
     * This reset is used when the motor disappears and therefore
     * must not force a new calibration if a valid baseline already
     * exists.
     */

    s_system.warmup_count = 0U;
    s_system.bin_valid_count = 0U;

    s_system.rms_stats = (system_online_stats_t){0};
    s_system.kurtosis_stats = (system_online_stats_t){0};
    s_system.bin_stats = (system_online_stats_t){0};

    s_system.diagnostics = (system_diagnostics_t){0};

    s_system.consecutive_abnormal = 0U;
    s_system.consecutive_normal = 0U;
    s_system.consecutive_no_motor = 0U;
}

static bool process_motor_presence(const dsp_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (result->peak_valid) {

        /*
         * A valid vibration detection breaks the consecutive
         * no-motor sequence.
         */
        s_system.consecutive_no_motor = 0U;

        return true;
    }

    /*
     * No valid vibration detected.
     */
    s_system.consecutive_no_motor++;

    ESP_LOGD(
        TAG,
        "invalid vibration: %u/%u",
        s_system.consecutive_no_motor,
        SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT
    );

    if (s_system.consecutive_no_motor >=
        SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT) {

        if (s_system.state != SYSTEM_STATE_NO_MOTOR) {

            ESP_LOGW(
                TAG,
                "no valid vibration for %u consecutive evaluations",
                SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT
            );

            s_system.state = SYSTEM_STATE_NO_MOTOR;

            /*
             * Clear the current monitoring context but preserve
             * an already validated baseline.
             */
            reset_monitoring_context();

            s_system.consecutive_no_motor =
                SYSTEM_NO_MOTOR_CONSECUTIVE_COUNT;

            ESP_LOGW(
                TAG,
                "state changed: %s",
                state_to_string(s_system.state)
            );
        }
    }

    return false;
}

static void buzzer_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << SYSTEM_BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(SYSTEM_BUZZER_GPIO, 0));

    if (xTaskCreatePinnedToCore(
            task_buzzer,
            "buzzer",
            2048,
            NULL,
            5,
            &s_buzzer_task,
            1) != pdPASS) {

        ESP_LOGE(TAG, "failed to create buzzer task");
        s_buzzer_task = NULL;
    }
}

static void task_buzzer(void *arg)
{
    (void)arg;

    while (true) {

        if (s_system.state != SYSTEM_STATE_ALARM) {

            gpio_set_level(SYSTEM_BUZZER_GPIO, 0);

            vTaskDelay(pdMS_TO_TICKS(100U));

            continue;
        }

        gpio_set_level(SYSTEM_BUZZER_GPIO, 1);

        vTaskDelay(
            pdMS_TO_TICKS(SYSTEM_BUZZER_ON_TIME_MS)
        );

        gpio_set_level(SYSTEM_BUZZER_GPIO, 0);

        vTaskDelay(
            pdMS_TO_TICKS(SYSTEM_BUZZER_OFF_TIME_MS)
        );

        if (s_system.state != SYSTEM_STATE_ALARM) {
            continue;
        }

        gpio_set_level(SYSTEM_BUZZER_GPIO, 1);

        vTaskDelay(
            pdMS_TO_TICKS(SYSTEM_BUZZER_ON_TIME_MS)
        );

        gpio_set_level(SYSTEM_BUZZER_GPIO, 0);

        vTaskDelay(
            pdMS_TO_TICKS(SYSTEM_BUZZER_PAUSE_TIME_MS)
        );
    }
}

static void buzzer_start(void)
{
    ESP_LOGW(TAG, "buzzer alarm activated");
}

static void buzzer_stop(void)
{
    gpio_set_level(SYSTEM_BUZZER_GPIO, 0);
}