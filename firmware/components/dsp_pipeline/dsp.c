/**
 * @file dsp.c
 * @brief DSP task implementation.
 */

#include "dsp.h"

#include "app_context.h"
#include "dsps_fft2r.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "portmacro.h"
#include <stdint.h>
#include <string.h>

#include <float.h>
#include <math.h>

#include <esp_dsp.h>

/* ============================================================================
* Private constants and macros  
* ========================================================================== */


#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define ACCEL_SENSITIVITY_G_PER_LSB    (0.000061f)
#define HANN_COHERENT_GAIN    (0.5f)
#define LSM6DS3TRC_SENSITIVITY_2G    (0.061f / 1000.0f)
#define ACCEL_SAMPLE_RATE_HZ    (3200.0f)

#define RPM_SEARCH_MIN_HZ    (5.0f)
#define RPM_SEARCH_MAX_HZ    (60.0f)
#define FFT_MIN_VALID_AMPLITUDE_G 0.005f

static const char *TAG = "dsp";

typedef struct
{
    float mean;
    float rms;
    float stddev;
    float minimum;
    float maximum;
    float peak_to_peak;

    float crest_factor;
    float kurtosis;

} dsp_time_stats_t;
typedef struct
{
    float bin;
    float frequency_hz;
    float rpm;
    float amplitude_g;

    bool valid;

} fft_peak_t;

typedef struct
{
    float time_signal[ACCEL_BLOCK_SIZE];

    float hann_window[ACCEL_BLOCK_SIZE];

    float hann_signal[ACCEL_BLOCK_SIZE];

    float fft_buffer[ACCEL_BLOCK_SIZE * 2];

    float magnitude[ACCEL_BLOCK_SIZE / 2];

    float frequency_axis[ACCEL_BLOCK_SIZE / 2];

    dsp_time_stats_t time_stats;

} dsp_context_t;


/* ============================================================================
* Private variables
* ========================================================================== */


static dsp_context_t s_dsp;
 
/* ============================================================================ 
* Private function prototypes
* ========================================================================== */
 
static float remove_dc_offset(const accel_block_t *input,
    float *output);

static void init_hann_window(float *window);

static esp_err_t init_fft(void);

static void build_frequency_axis(float *frequency_axis);

static void calculate_time_stats(const float *signal, dsp_time_stats_t *stats);

static void analyze_time_signal(const float *signal, dsp_time_stats_t *stats);

static void calculate_rms(const float *signal, dsp_time_stats_t *stats);

static void calculate_min_max(const float *signal, dsp_time_stats_t *stats);
static void calculate_stddev(const float *signal,
                             dsp_time_stats_t *stats);

static void calculate_crest_factor(dsp_time_stats_t *stats);

static void calculate_kurtosis(const float *signal, dsp_time_stats_t *stats);

static void calculate_basic_stats(const float *signal, dsp_time_stats_t *stats);

static void calculate_crest_factor(dsp_time_stats_t *stats);

static void calculate_kurtosis(const float *signal, dsp_time_stats_t *stats);

static fft_peak_t analyze_fft(const float *hann_window,
                              float *hann_signal,
                              float *fft_buffer,
                              float *magnitude,
                              const float *frequency_axis);

static void apply_hann_window(const float *window, float *signal);

static void convert_to_g(float *signal);

static void prepare_fft_input(const float *signal, float *fft_buffer);

static void execute_fft(float *fft_buffer);

static void calculate_fft_magnitude(const float *fft_buffer,
    float *magnitude);

static void normalize_fft(float *magnitude);

static fft_peak_t find_peak(
    const float *magnitude,
    const float *frequency_axis,
    float min_frequency,
    float max_frequency);

static fft_peak_t interpolate_peak(
    const float *magnitude,
    const float *frequency_axis,
    uint16_t peak_bin);

static void validate_peak(fft_peak_t *peak);
/* ============================================================================
 * Public function implementations
 * ========================================================================== */

void task_dsp(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    if (ctx == NULL || 
        ctx->queue_accel_block_to_dsp == NULL ||
        ctx->queue_dsp_to_dac == NULL ||
        ctx->queue_dsp_to_hmi == NULL ||
        ctx->queue_dsp_to_storage == NULL) {
        ESP_LOGE(TAG, "invalid DSP context");
        vTaskDelete(NULL);
    }

    init_hann_window(s_dsp.hann_window);

    if (init_fft() != ESP_OK) {
        vTaskDelete(NULL);
    }

    build_frequency_axis(s_dsp.frequency_axis);

    ESP_LOGI(TAG, "task started");
    while (true) {
        accel_block_t block;
        if (xQueueReceive(ctx->queue_accel_block_to_dsp, &block,
             portMAX_DELAY) == pdTRUE) {
            /* TODO: process block and overwrite queue_dsp_result. */
            s_dsp.time_stats.mean = 
                remove_dc_offset(&block, s_dsp.time_signal);
        
            convert_to_g(s_dsp.time_signal);

            calculate_time_stats(s_dsp.time_signal,
                     &s_dsp.time_stats);

            analyze_time_signal( s_dsp.time_signal,
                &s_dsp.time_stats);
            // ESP_LOGD(TAG, "Centered: [%.2f, %.2f, %.2f, %.2f",
            //     s_dsp.time_signal[0],
            //     s_dsp.time_signal[1],
            //     s_dsp.time_signal[2],
            //     s_dsp.time_signal[3]);
            

            ESP_LOGD(TAG,
                "Mean: %.2f | RMS: %.6f | Min: %.6f | Max: %.6f | PkPk: %.6f",
                s_dsp.time_stats.mean,
                s_dsp.time_stats.rms,
                s_dsp.time_stats.minimum,
                s_dsp.time_stats.maximum,
                s_dsp.time_stats.peak_to_peak);


            memcpy(s_dsp.hann_signal, s_dsp.time_signal, sizeof(s_dsp.time_signal));

            fft_peak_t peak = analyze_fft(
                s_dsp.hann_window,
                s_dsp.hann_signal,
                s_dsp.fft_buffer,
                s_dsp.magnitude,
                s_dsp.frequency_axis);

            if (peak.valid)
            {
                ESP_LOGI(TAG, "Peak: %.2f Hz | %.1f RPM | %.5f g",
                    peak.frequency_hz, peak.rpm, peak.amplitude_g);

            } else
            {
                ESP_LOGI(TAG, "No valid vibration detected");
            }

        }
    }
}

/* ============================================================================
 * Private function implementations
 * ========================================================================== */
static void analyze_time_signal(const float *signal,
                                dsp_time_stats_t *stats)
{
    calculate_rms(signal, stats);

    calculate_min_max(signal, stats);

    calculate_stddev(signal, stats);

    calculate_crest_factor(stats);

    calculate_kurtosis(signal, stats);
}
static fft_peak_t analyze_fft(const float *hann_window,
    float *hann_signal,
    float *fft_buffer,
    float *magnitude,
    const float *frequency_axis)
{

    apply_hann_window(hann_window, hann_signal);

    prepare_fft_input(hann_signal, fft_buffer);

    execute_fft(fft_buffer);

    calculate_fft_magnitude(fft_buffer, magnitude);

    normalize_fft(magnitude);

    fft_peak_t peak = find_peak(
        magnitude, frequency_axis, RPM_SEARCH_MIN_HZ, RPM_SEARCH_MAX_HZ);

    peak = interpolate_peak(
        magnitude, frequency_axis, (uint16_t)peak.bin);

    validate_peak(&peak);

    return peak;
}

static void init_hann_window(float *window)
{
    const float denominator = (float)(ACCEL_BLOCK_SIZE - 1U);

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        window[i] =
            0.5f * (1.0f - cosf((2.0f * M_PI * (float)i) / denominator));
    }
}


static esp_err_t init_fft(void)
{
    esp_err_t ret;

    ret = dsps_fft2r_init_fc32(NULL, ACCEL_BLOCK_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "FFT initialization failed (%s)",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG,
             "FFT initialized (%u points)",
             ACCEL_BLOCK_SIZE);

    return ESP_OK;
}

static void build_frequency_axis(float *frequency_axis)
{
    const float bin_width =
        ACCEL_SAMPLE_RATE_HZ / (float)ACCEL_BLOCK_SIZE;

    for (uint16_t i = 0; i < (ACCEL_BLOCK_SIZE / 2U); i++) {
        frequency_axis[i] = (float)i * bin_width;
    }
}

 static float remove_dc_offset(const accel_block_t *input, float *output)
{
    float mean = 0.0f;

    /* Calculate block mean. */
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        mean += (float)input->samples[i];
    }
    mean /= (float)ACCEL_BLOCK_SIZE;

    /* Convert to float and remove DC offset. */
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        output[i] = (float)input->samples[i] - mean;
    }

    return mean;
}

static void convert_to_g(float *signal)
{
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; i++) {
        signal[i] *= LSM6DS3TRC_SENSITIVITY_2G;
    }
}

static void calculate_time_stats(const float *signal,
                                 dsp_time_stats_t *stats)
{
    float sum_squares = 0.0f;

    stats->minimum = FLT_MAX;
    stats->maximum = -FLT_MAX;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        const float sample = signal[i];

        sum_squares += sample * sample;

        if (sample < stats->minimum) {
            stats->minimum = sample;
        }

        if (sample > stats->maximum) {
            stats->maximum = sample;
        }
    }

    stats->rms = sqrtf(sum_squares / (float)ACCEL_BLOCK_SIZE);

    stats->peak_to_peak = stats->maximum - stats->minimum;

    /* Calculated in a future step. */
    stats->stddev = 0.0f;
}

static void apply_hann_window(const float *window,
                              float *signal)
{
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        signal[i] *= window[i];
    }
}

static void prepare_fft_input(const float *signal,
                              float *fft_buffer)
{
    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; ++i) {
        fft_buffer[2U * i] = signal[i];
        fft_buffer[(2U * i) + 1U] = 0.0f;
    }
}

static void execute_fft(float *fft_buffer)
{
    dsps_fft2r_fc32(fft_buffer, ACCEL_BLOCK_SIZE);

    dsps_bit_rev_fc32(fft_buffer, ACCEL_BLOCK_SIZE);

    dsps_cplx2reC_fc32(fft_buffer, ACCEL_BLOCK_SIZE);
}

static void calculate_fft_magnitude(const float *fft_buffer,
                                    float *magnitude)
{
    for (uint16_t i = 0; i < (ACCEL_BLOCK_SIZE / 2); i++) {

        const float real = fft_buffer[2U * i];
        const float imag = fft_buffer[(2U * i) + 1U];

        magnitude[i] = sqrtf((real * real) +
                             (imag * imag));
    }
}

static void normalize_fft(float *magnitude)
{
    const float scale =
        1.0f / ((float)ACCEL_BLOCK_SIZE * HANN_COHERENT_GAIN);

    /* DC */
    magnitude[0] *= scale;

    /* Positive frequencies */
    for (uint16_t i = 1; i < (ACCEL_BLOCK_SIZE / 2U) - 1U; i++) {
        magnitude[i] *= (2.0f * scale);
    }

    /* Nyquist */
    magnitude[(ACCEL_BLOCK_SIZE / 2U) - 1U] *= scale;
}

static fft_peak_t find_peak(const float *magnitude,
                            const float *frequency_axis,
                            float min_frequency,
                            float max_frequency)
{
    fft_peak_t peak = {0};

    float peak_amplitude = -FLT_MAX;
    uint16_t peak_bin = 0U;

    for (uint16_t i = 0; i < (ACCEL_BLOCK_SIZE / 2U); i++) {

        const float frequency = frequency_axis[i];

        if ((frequency < min_frequency) ||
            (frequency > max_frequency)) {
            continue;
        }

        if (magnitude[i] > peak_amplitude) {
            peak_amplitude = magnitude[i];
            peak_bin = i;
        }
    }

    peak.bin = (float)peak_bin;
    peak.frequency_hz = frequency_axis[peak_bin];
    peak.rpm = peak.frequency_hz *60.0f;
    peak.amplitude_g = magnitude[peak_bin];

    return peak;
}

static fft_peak_t interpolate_peak(const float *magnitude,
                                   const float *frequency_axis,
                                   uint16_t peak_bin)
{
    fft_peak_t peak;

    /* Não é possível interpolar nas extremidades */
    if ((peak_bin == 0U) ||
        (peak_bin >= (ACCEL_BLOCK_SIZE / 2U) - 1U)) {

        peak.bin = (float)peak_bin;
        peak.frequency_hz = frequency_axis[peak_bin];
        peak.rpm = peak.frequency_hz *60.0f;
        peak.amplitude_g = magnitude[peak_bin];

        return peak;
    }

    const float left   = magnitude[peak_bin - 1U];
    const float center = magnitude[peak_bin];
    const float right  = magnitude[peak_bin + 1U];

    const float denominator =
        (left - (2.0f * center) + right);

    float delta = 0.0f;

    if (fabsf(denominator) > 1e-12f) 
    {
        delta = 0.5f * (left - right) / denominator;
    }
    if (delta > 0.5f) 
    {
        delta = 0.5f;
    }

    if (delta < -0.5f) 
    {
        delta = -0.5f;
    }

    peak.bin = (float)peak_bin + delta;

    const float bin_width =
        ACCEL_SAMPLE_RATE_HZ /
        (float)ACCEL_BLOCK_SIZE;

    peak.frequency_hz =
        peak.bin * bin_width;
    peak.rpm = peak.frequency_hz *60.0f;

    /*
     * Interpolação parabólica da amplitude.
     */
    peak.amplitude_g =
        center -
        0.25f *
        (left - right) *
        delta;

    return peak;
}

static void validate_peak(fft_peak_t *peak)
{
    if (peak->amplitude_g < FFT_MIN_VALID_AMPLITUDE_G) {
        peak->valid = false;
        return;
    }

    peak->valid = true;
}

static void calculate_rms(const float *signal,
                          dsp_time_stats_t *stats)
{
    float sum_squares = 0.0f;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; i++) {
        sum_squares += signal[i] * signal[i];
    }

    stats->rms = sqrtf(sum_squares / (float)ACCEL_BLOCK_SIZE);
}

static void calculate_min_max(const float *signal,
                              dsp_time_stats_t *stats)
{
    stats->minimum = FLT_MAX;
    stats->maximum = -FLT_MAX;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; i++) {

        if (signal[i] < stats->minimum) {
            stats->minimum = signal[i];
        }

        if (signal[i] > stats->maximum) {
            stats->maximum = signal[i];
        }
    }

    stats->peak_to_peak =
        stats->maximum - stats->minimum;
}

static void calculate_stddev(const float *signal,
                             dsp_time_stats_t *stats)
{
    float variance = 0.0f;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; i++) {

        const float error =
            signal[i] - stats->mean;

        variance += error * error;
    }

    variance /= (float)ACCEL_BLOCK_SIZE;

    stats->stddev = sqrtf(variance);
}

static void calculate_crest_factor(dsp_time_stats_t *stats)
{
    const float peak =
        fmaxf(fabsf(stats->minimum),
              fabsf(stats->maximum));

    if (stats->rms > FLT_EPSILON) {
        stats->crest_factor = peak / stats->rms;
    }
    else {
        stats->crest_factor = 0.0f;
    }
}

static void calculate_kurtosis(const float *signal,
                               dsp_time_stats_t *stats)
{
    if (stats->stddev < FLT_EPSILON) {
        stats->kurtosis = 0.0f;
        return;
    }

    float sum = 0.0f;

    for (uint16_t i = 0; i < ACCEL_BLOCK_SIZE; i++) {

        const float normalized =
            (signal[i] - stats->mean) /
            stats->stddev;

        sum += normalized *
               normalized *
               normalized *
               normalized;
    }

    stats->kurtosis =
        sum / (float)ACCEL_BLOCK_SIZE;
}