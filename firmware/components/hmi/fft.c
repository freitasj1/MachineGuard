/**
 * @file fft.c
 * @brief Implementação da tela de espectro FFT.
 */

#include "fft.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "hmi.h"

static const char *TAG = "fft";

/* ========================================================================== */
/* Layout                                                                     */
/* ========================================================================== */

#define FFT_LCD_WIDTH          480U
#define FFT_LCD_HEIGHT         320U

#define FFT_HEADER_HEIGHT      30U
#define FFT_FOOTER_HEIGHT      24U

#define FFT_PANEL_X            6U
#define FFT_PANEL_Y            37U
#define FFT_PANEL_WIDTH        468U
#define FFT_PANEL_HEIGHT       202U

#define FFT_GRAPH_X            20U
#define FFT_GRAPH_Y            58U
#define FFT_GRAPH_WIDTH        440U
#define FFT_GRAPH_HEIGHT       166U

#define FFT_GRAPH_BASELINE_Y   224U

#define FFT_FIRST_BIN          2U
#define FFT_LAST_BIN           46U
#define FFT_BIN_COUNT          \
    (FFT_LAST_BIN - FFT_FIRST_BIN + 1U)

#define FFT_BAR_GAP            1U

/* ========================================================================== */
/* Cache                                                                      */
/* ========================================================================== */

typedef struct {
    bool initialized;
    bool cache_valid;

    float magnitude[FFT_BIN_COUNT];
} fft_cache_t;

static fft_cache_t s_cache;

/* ========================================================================== */
/* Helpers                                                                    */
/* ========================================================================== */

static bool magnitude_valid(
    float value
)
{
    return value >= 0.0f &&
           value == value;
}

static uint16_t calculate_bar_height(
    float magnitude,
    float maximum_magnitude
)
{
    if (!magnitude_valid(magnitude) ||
        !magnitude_valid(maximum_magnitude) ||
        maximum_magnitude <= 0.0f) {

        return 0U;
    }

    float normalized =
        magnitude / maximum_magnitude;

    if (normalized > 1.0f) {
        normalized = 1.0f;
    }

    if (normalized < 0.0f) {
        normalized = 0.0f;
    }

    return (uint16_t)(
        normalized *
        (float)(FFT_GRAPH_HEIGHT - 2U)
    );
}

static void find_maximum_magnitude(
    const float *magnitude,
    float *maximum_magnitude
)
{
    *maximum_magnitude = 0.0f;

    for (uint16_t i = 0U; i < FFT_BIN_COUNT; i++) {

        if (!magnitude_valid(magnitude[i])) {
            continue;
        }

        if (magnitude[i] > *maximum_magnitude) {
            *maximum_magnitude = magnitude[i];
        }
    }
}

static void find_highlight_bins(
    const float *magnitude,
    uint16_t *highlight_bins
)
{
    float best[3] = {
        -1.0f,
        -1.0f,
        -1.0f
    };

    highlight_bins[0] = UINT16_MAX;
    highlight_bins[1] = UINT16_MAX;
    highlight_bins[2] = UINT16_MAX;

    for (uint16_t i = 0U; i < FFT_BIN_COUNT; i++) {

        const float value = magnitude[i];

        if (!magnitude_valid(value)) {
            continue;
        }

        if (value > best[0]) {

            best[2] = best[1];
            highlight_bins[2] = highlight_bins[1];

            best[1] = best[0];
            highlight_bins[1] = highlight_bins[0];

            best[0] = value;
            highlight_bins[0] = i;

        } else if (value > best[1]) {

            best[2] = best[1];
            highlight_bins[2] = highlight_bins[1];

            best[1] = value;
            highlight_bins[1] = i;

        } else if (value > best[2]) {

            best[2] = value;
            highlight_bins[2] = i;
        }
    }
}

static bool is_highlight_bin(
    uint16_t index,
    const uint16_t *highlight_bins
)
{
    for (uint16_t i = 0U; i < 3U; i++) {

        if (highlight_bins[i] == index) {
            return true;
        }
    }

    return false;
}

/* ========================================================================== */
/* Static layout                                                              */
/* ========================================================================== */

static esp_err_t draw_static_layout(void)
{
    esp_err_t err;

    /*
     * Complete background.
     */
    err = hmi_display_fill_rect(
        0,
        0,
        FFT_LCD_WIDTH,
        FFT_LCD_HEIGHT,
        HMI_COLOR_BLACK
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Header.
     */
    err = hmi_display_fill_rect(
        0,
        0,
        FFT_LCD_WIDTH,
        FFT_HEADER_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        6,
        8,
        "MACHINEGUARD",
        HMI_FONT_MEDIUM,
        HMI_COLOR_BLUE
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        300,
        9,
        "FFT",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    char rpm_text[32];



    err = hmi_display_draw_text(
        454,
        9,
        "2/2",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Main panel.
     */
    err = hmi_display_fill_rect(
        FFT_PANEL_X,
        FFT_PANEL_Y,
        FFT_PANEL_WIDTH,
        FFT_PANEL_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Title.
     */
    err = hmi_display_draw_text(
        FFT_PANEL_X + 10U,
        FFT_PANEL_Y + 10U,
        "FFT MAGNITUDE",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Vertical frequency grid.
     */
    err = hmi_display_draw_vline(
        20U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        160U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        311U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        460U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Graph baseline.
     */
    err = hmi_display_draw_hline(
        FFT_GRAPH_X,
        FFT_GRAPH_BASELINE_Y,
        FFT_GRAPH_WIDTH,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Frequency labels.
     */
    err = hmi_display_draw_text(
        12U,
        230U,
        "5",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        150U,
        230U,
        "50",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        300U,
        230U,
        "100",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        445U,
        230U,
        "150 Hz",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Footer.
     */
    err = hmi_display_fill_rect(
        0,
        FFT_LCD_HEIGHT - FFT_FOOTER_HEIGHT,
        FFT_LCD_WIDTH,
        FFT_FOOTER_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        8,
        302,
        "[A] PROX TELA",
        HMI_FONT_SMALL,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        385,
        302,
        "[B] PAUSE",
        HMI_FONT_SMALL,
        HMI_COLOR_BLUE
    );
}

/* ========================================================================== */
/* Dynamic graph                                                              */
/* ========================================================================== */

static esp_err_t draw_spectrum(
    const float *magnitude
)
{
    float maximum_magnitude = 0.0f;

    uint16_t highlight_bins[3];

    find_maximum_magnitude(
        magnitude,
        &maximum_magnitude
    );

    find_highlight_bins(
        magnitude,
        highlight_bins
    );

    /*
     * Clear only the graph area.
     *
     * The static layout remains untouched.
     */
    esp_err_t err = hmi_display_fill_rect(
        FFT_GRAPH_X + 1U,
        FFT_GRAPH_Y,
        FFT_GRAPH_WIDTH - 2U,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Restore static graph lines.
     */
    err = hmi_display_draw_vline(
        FFT_GRAPH_X,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        160U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        311U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_vline(
        460U,
        FFT_GRAPH_Y,
        FFT_GRAPH_HEIGHT,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_hline(
        FFT_GRAPH_X,
        FFT_GRAPH_BASELINE_Y,
        FFT_GRAPH_WIDTH,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    if (maximum_magnitude <= 0.0f) {
        return ESP_OK;
    }

    const uint16_t total_gaps =
        (FFT_BIN_COUNT - 1U) *
        FFT_BAR_GAP;

    const uint16_t available_width =
        FFT_GRAPH_WIDTH - 1U;

    uint16_t bar_width =
        (available_width - total_gaps) /
        FFT_BIN_COUNT;

    if (bar_width == 0U) {
        bar_width = 1U;
    }

    for (uint16_t i = 0U; i < FFT_BIN_COUNT; i++) {

        const uint16_t height =
            calculate_bar_height(
                magnitude[i],
                maximum_magnitude
            );

        if (height == 0U) {
            continue;
        }

        const uint16_t x =
            FFT_GRAPH_X + 1U +
            i * (bar_width + FFT_BAR_GAP);

        const uint16_t y =
            FFT_GRAPH_BASELINE_Y -
            height;

        const hmi_color_t color =
            is_highlight_bin(
                i,
                highlight_bins
            )
                ? HMI_COLOR_YELLOW
                : HMI_COLOR_BLUE;

        err = hmi_display_fill_rect(
            x,
            y,
            bar_width,
            height,
            color
        );

        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

esp_err_t fft_init(void)
{
    memset(
        &s_cache,
        0,
        sizeof(s_cache)
    );

    esp_err_t err = draw_static_layout();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to draw FFT layout: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    s_cache.initialized = true;
    s_cache.cache_valid = false;

    ESP_LOGI(
        TAG,
        "FFT screen initialized"
    );

    return ESP_OK;
}

esp_err_t fft_update(
    const hmi_data_t *data
)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_cache.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool spectrum_changed =
        !s_cache.cache_valid;

    if (!spectrum_changed) {

        for (uint16_t i = 0U; i < FFT_BIN_COUNT; i++) {

            if (data->fft_magnitude[i] !=
                s_cache.magnitude[i]) {

                spectrum_changed = true;
                break;
            }
        }
    }

    if (!spectrum_changed) {
        return ESP_OK;
    }

    esp_err_t err = draw_spectrum(
        data->fft_magnitude
    );

    if (err != ESP_OK) {
        return err;
    }

    memcpy(
        s_cache.magnitude,
        data->fft_magnitude,
        sizeof(s_cache.magnitude)
    );

    s_cache.cache_valid = true;

    return ESP_OK;
}

static esp_err_t draw_header_rpm(int rpm)
{
    char text[16];

    snprintf(
        text,
        sizeof(text),
        "%d",
        rpm
    );

    /*
     * Clear only the value region.
     */
    esp_err_t err = hmi_display_fill_rect(
        292,
        5,
        58,
        18,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        292,
        9,
        text,
        HMI_FONT_SMALL,
        HMI_COLOR_WHITE
    );
}