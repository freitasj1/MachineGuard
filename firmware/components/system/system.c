/**
 * @file system.c
 * @brief System decision task implementation.
 */

#include "system.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_context.h"
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

    uint8_t consecutive_abnormal;
    uint8_t consecutive_normal;

} system_context_t;

/* ============================================================================
 * Private variables
 * ========================================================================== */

static system_context_t s_system;

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

static void log_baseline(void);

static const char *state_to_string(system_state_t state);

/* ============================================================================
 * Public function implementations
 * ========================================================================== */

void task_system(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL ||
        ctx->queue_dsp_to_system == NULL) {

        ESP_LOGE(TAG, "invalid system context");
        vTaskDelete(NULL);
        return;
    }

    reset_context();

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

            default:

                ESP_LOGE(
                    TAG,
                    "invalid system state: %d",
                    s_system.state
                );

                reset_context();

                break;
        }
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

    s_system.consecutive_abnormal = 0U;
    s_system.consecutive_normal = 0U;
}
static void process_warmup(const dsp_result_t *result)
{
    if (result == NULL) {
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

    if (result->peak_valid) {

        update_online_stats(
            &s_system.bin_stats,
            result->bin_1xrpm_amplitude
        );

        s_system.bin_valid_count++;
    }

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
            * Keep the system out of HEALTHY until a valid baseline
            * is available.
            */
            s_system.state = SYSTEM_STATE_WARMUP;
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
     * The current detector is one-sided:
     * values above the healthy baseline are considered abnormal.
     */
    return zscore > SYSTEM_ZSCORE_THRESHOLD;
}

static bool evaluate_abnormality(const dsp_result_t *result)
{
    if (result == NULL || !s_system.baseline.valid) {
        return false;
    }

    uint8_t abnormal_features = 0U;

    if (evaluate_feature(
            result->rms,
            &s_system.baseline.rms)) {

        abnormal_features++;
    }

    if (evaluate_feature(
            result->kurtosis,
            &s_system.baseline.kurtosis)) {

        abnormal_features++;
    }

    if (evaluate_feature(
            result->bin_1xrpm_amplitude,
            &s_system.baseline.bin_1xrpm_amplitude)) {

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

        default:
            return "UNKNOWN";
    }
}