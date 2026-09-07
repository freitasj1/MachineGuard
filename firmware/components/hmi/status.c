/**
 * @file status.c
 * @brief Implementação da tela principal de status.
 */

#include "status.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hmi.h"
#include "esp_log.h"

static const char *TAG = "status";

/* ========================================================================== */
/* Layout                                                                     */
/* ========================================================================== */

#define STATUS_LCD_WIDTH       480U
#define STATUS_LCD_HEIGHT      320U

#define STATUS_HEADER_HEIGHT   30U
#define STATUS_FOOTER_HEIGHT   24U

#define STATUS_LEFT_X          6U
#define STATUS_LEFT_WIDTH      320U

#define STATUS_RIGHT_X         334U
#define STATUS_RIGHT_WIDTH     140U

#define STATUS_CARD_HEIGHT     76U

#define STATUS_CARD1_Y         37U
#define STATUS_CARD2_Y         119U
#define STATUS_CARD3_Y         201U

/* ========================================================================== */
/* Cache                                                                      */
/* ========================================================================== */

typedef struct {
    bool initialized;

    int kurtosis_x100;
    int rms_x10;
    int rpm_amplitude_x10;

    int rms_z_x10;
    int kurtosis_z_x10;
    int rpm_z_x10;

    int rpm;

    int temperature_x10;
    bool temperature_valid;

    bool rms_abnormal;
    bool kurtosis_abnormal;
    bool rpm_abnormal;

    system_state_t state;
} status_cache_t;

static status_cache_t s_cache;

/* ========================================================================== */
/* Helpers                                                                    */
/* ========================================================================== */

static int quantize_x10(float value)
{
    if (value >= 0.0f) {
        return (int)(value * 10.0f + 0.5f);
    }

    return (int)(value * 10.0f - 0.5f);
}

static int quantize_x100(float value)
{
    if (value >= 0.0f) {
        return (int)(value * 100.0f + 0.5f);
    }

    return (int)(value * 100.0f - 0.5f);
}

static hmi_color_t feature_color(
    bool abnormal
)
{
    if (abnormal) {
        return HMI_COLOR_RED;
    }

    return HMI_COLOR_GREEN;
}

static const char *state_text(
    system_state_t state
)
{
    switch (state) {

        case SYSTEM_STATE_WARMUP:
            return "WARMUP";

        case SYSTEM_STATE_HEALTHY:
            return "NORMAL";

        case SYSTEM_STATE_ALARM:
            return "ALARM";

        case SYSTEM_STATE_INIT:
        default:
            return "INIT";
    }
}

static hmi_color_t state_color(
    system_state_t state
)
{
    switch (state) {

        case SYSTEM_STATE_WARMUP:
            return HMI_COLOR_YELLOW;

        case SYSTEM_STATE_HEALTHY:
            return HMI_COLOR_GREEN;

        case SYSTEM_STATE_ALARM:
            return HMI_COLOR_RED;

        case SYSTEM_STATE_INIT:
        default:
            return HMI_COLOR_WHITE;
    }
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
        STATUS_LCD_WIDTH,
        STATUS_LCD_HEIGHT,
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
        STATUS_LCD_WIDTH,
        STATUS_HEADER_HEIGHT,
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
        270,
        9,
        "RPM",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        355,
        9,
        "C",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        454,
        9,
        "1/2",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Left cards.
     */
    err = hmi_display_fill_rect(
        STATUS_LEFT_X,
        STATUS_CARD1_Y,
        STATUS_LEFT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X,
        STATUS_CARD2_Y,
        STATUS_LEFT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X,
        STATUS_CARD3_Y,
        STATUS_LEFT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Labels.
     */
    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD1_Y + 10U,
        "KURTOSIS",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD2_Y + 10U,
        "RMS ACCEL",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD3_Y + 10U,
        "1X RPM",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Separators.
     */
    err = hmi_display_draw_hline(
        STATUS_LEFT_X + 5U,
        STATUS_CARD1_Y + 58U,
        310U,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_hline(
        STATUS_LEFT_X + 5U,
        STATUS_CARD2_Y + 58U,
        310U,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_hline(
        STATUS_LEFT_X + 5U,
        STATUS_CARD3_Y + 58U,
        310U,
        HMI_COLOR_DARK_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Right cards.
     */
    err = hmi_display_fill_rect(
        STATUS_RIGHT_X,
        STATUS_CARD1_Y,
        STATUS_RIGHT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_RIGHT_X,
        STATUS_CARD2_Y,
        STATUS_RIGHT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_RIGHT_X,
        STATUS_CARD3_Y,
        STATUS_RIGHT_WIDTH,
        STATUS_CARD_HEIGHT,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * Status label.
     */
    err = hmi_display_draw_text(
        STATUS_RIGHT_X + 10U,
        STATUS_CARD1_Y + 10U,
        "STATUS",
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
        STATUS_LCD_HEIGHT - STATUS_FOOTER_HEIGHT,
        STATUS_LCD_WIDTH,
        STATUS_FOOTER_HEIGHT,
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
/* Dynamic regions                                                            */
/* ========================================================================== */

static esp_err_t draw_header_rpm(
    int rpm
)
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

static esp_err_t draw_header_temperature(
    float temperature_c,
    bool valid
)
{
    char text[16];

    if (valid) {
        snprintf(
            text,
            sizeof(text),
            "%.1f",
            temperature_c
        );
    } else {
        snprintf(
            text,
            sizeof(text),
            "--"
        );
    }

    esp_err_t err = hmi_display_fill_rect(
        365,
        5,
        55,
        18,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        365,
        9,
        text,
        HMI_FONT_SMALL,
        HMI_COLOR_WHITE
    );
}

static esp_err_t draw_kurtosis(
    float value,
    float zscore,
    bool abnormal
)
{
    char value_text[20];
    char z_text[20];

    snprintf(
        value_text,
        sizeof(value_text),
        "%.2f",
        value
    );

    snprintf(
        z_text,
        sizeof(z_text),
        "Z = %.1f",
        zscore
    );

    esp_err_t err = hmi_display_fill_rect(
        STATUS_LEFT_X + 8U,
        STATUS_CARD1_Y + 27U,
        135U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 145U,
        STATUS_CARD1_Y + 27U,
        80U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 225U,
        STATUS_CARD1_Y + 60U,
        85U,
        13U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD1_Y + 30U,
        value_text,
        HMI_FONT_LARGE,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 150U,
        STATUS_CARD1_Y + 34U,
        abnormal ? "ALARM" : "OK",
        HMI_FONT_MEDIUM,
        feature_color(abnormal)
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_LEFT_X + 230U,
        STATUS_CARD1_Y + 63U,
        z_text,
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );
}

static esp_err_t draw_rms(
    float value,
    float zscore,
    bool abnormal
)
{
    char value_text[20];
    char z_text[20];

    snprintf(
        value_text,
        sizeof(value_text),
        "%.1f",
        value
    );

    snprintf(
        z_text,
        sizeof(z_text),
        "Z = %.1f",
        zscore
    );

    esp_err_t err = hmi_display_fill_rect(
        STATUS_LEFT_X + 8U,
        STATUS_CARD2_Y + 27U,
        135U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 145U,
        STATUS_CARD2_Y + 27U,
        80U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 225U,
        STATUS_CARD2_Y + 60U,
        85U,
        13U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD2_Y + 30U,
        value_text,
        HMI_FONT_LARGE,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 90U,
        STATUS_CARD2_Y + 39U,
        "G",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 150U,
        STATUS_CARD2_Y + 34U,
        abnormal ? "ALARM" : "OK",
        HMI_FONT_MEDIUM,
        feature_color(abnormal)
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_LEFT_X + 230U,
        STATUS_CARD2_Y + 63U,
        z_text,
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );
}

static esp_err_t draw_1x_rpm(
    float value,
    float zscore,
    bool abnormal
)
{
    char value_text[20];
    char z_text[20];

    snprintf(
        value_text,
        sizeof(value_text),
        "%.1f",
        value
    );

    snprintf(
        z_text,
        sizeof(z_text),
        "Z = %.1f",
        zscore
    );

    esp_err_t err = hmi_display_fill_rect(
        STATUS_LEFT_X + 8U,
        STATUS_CARD3_Y + 27U,
        135U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 145U,
        STATUS_CARD3_Y + 27U,
        80U,
        27U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_fill_rect(
        STATUS_LEFT_X + 225U,
        STATUS_CARD3_Y + 60U,
        85U,
        13U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 10U,
        STATUS_CARD3_Y + 30U,
        value_text,
        HMI_FONT_LARGE,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 90U,
        STATUS_CARD3_Y + 39U,
        "G",
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_LEFT_X + 150U,
        STATUS_CARD3_Y + 34U,
        abnormal ? "ALARM" : "OK",
        HMI_FONT_MEDIUM,
        feature_color(abnormal)
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_LEFT_X + 230U,
        STATUS_CARD3_Y + 63U,
        z_text,
        HMI_FONT_SMALL,
        HMI_COLOR_GRAY
    );
}

static esp_err_t draw_state(
    system_state_t state
)
{
    esp_err_t err = hmi_display_fill_rect(
        STATUS_RIGHT_X + 8U,
        STATUS_CARD1_Y + 27U,
        STATUS_RIGHT_WIDTH - 16U,
        30U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_RIGHT_X + 10U,
        STATUS_CARD1_Y + 34U,
        state_text(state),
        HMI_FONT_MEDIUM,
        state_color(state)
    );
}

static esp_err_t draw_right_rpm(
    int rpm
)
{
    char text[16];

    snprintf(
        text,
        sizeof(text),
        "%d",
        rpm
    );

    esp_err_t err = hmi_display_fill_rect(
        STATUS_RIGHT_X + 10U,
        STATUS_CARD2_Y + 27U,
        STATUS_RIGHT_WIDTH - 20U,
        32U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_RIGHT_X + 27U,
        STATUS_CARD2_Y + 30U,
        text,
        HMI_FONT_LARGE,
        HMI_COLOR_WHITE
    );
}

static esp_err_t draw_right_temperature(
    float temperature_c,
    bool valid
)
{
    char text[16];

    if (valid) {
        snprintf(
            text,
            sizeof(text),
            "%.1f",
            temperature_c
        );
    } else {
        snprintf(
            text,
            sizeof(text),
            "--"
        );
    }

    esp_err_t err = hmi_display_fill_rect(
        STATUS_RIGHT_X + 10U,
        STATUS_CARD3_Y + 27U,
        STATUS_RIGHT_WIDTH - 20U,
        32U,
        HMI_COLOR_PANEL
    );

    if (err != ESP_OK) {
        return err;
    }

    err = hmi_display_draw_text(
        STATUS_RIGHT_X + 18U,
        STATUS_CARD3_Y + 30U,
        text,
        HMI_FONT_LARGE,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    if (!valid) {
        return ESP_OK;
    }

    err = hmi_display_draw_degree_symbol(
        STATUS_RIGHT_X + 91U,
        STATUS_CARD3_Y + 31U,
        HMI_COLOR_WHITE
    );

    if (err != ESP_OK) {
        return err;
    }

    return hmi_display_draw_text(
        STATUS_RIGHT_X + 103U,
        STATUS_CARD3_Y + 39U,
        "C",
        HMI_FONT_SMALL,
        HMI_COLOR_WHITE
    );
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

esp_err_t status_init(void)
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
            "Failed to draw STATUS layout: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    s_cache.initialized = true;

    /*
     * Force first dynamic update.
     *
     * The cache starts at zero, so most values will naturally update.
     * State is initialized explicitly to INIT.
     */
    s_cache.state = SYSTEM_STATE_INIT;

    ESP_LOGI(
        TAG,
        "STATUS screen initialized"
    );

    return ESP_OK;
}

esp_err_t status_update(
    const hmi_data_t *data
)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_cache.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const int rpm =
        (int)(data->features.rpm + 0.5f);

    const int temperature_x10 =
        quantize_x10(
            data->features.temperature_c
        );

    const int kurtosis_x100 =
        quantize_x100(
            data->features.kurtosis
        );

    const int rms_x10 =
        quantize_x10(
            data->features.rms
        );

    const int rpm_amplitude_x10 =
        quantize_x10(
            data->features.bin_1xrpm_amplitude
        );

    const int rms_z_x10 =
        quantize_x10(
            data->diagnostics.rms_zscore
        );

    const int kurtosis_z_x10 =
        quantize_x10(
            data->diagnostics.kurtosis_zscore
        );

    const int rpm_z_x10 =
        quantize_x10(
            data->diagnostics.bin_1xrpm_zscore
        );

    esp_err_t err;

    /*
     * Header RPM + right RPM.
     */
    if (rpm != s_cache.rpm) {

        err = draw_header_rpm(rpm);

        if (err != ESP_OK) {
            return err;
        }

        err = draw_right_rpm(rpm);

        if (err != ESP_OK) {
            return err;
        }

        s_cache.rpm = rpm;
    }

    /*
     * Temperature.
     */
    if (temperature_x10 != s_cache.temperature_x10 ||
        data->features.temperature_valid !=
            s_cache.temperature_valid) {

        err = draw_header_temperature(
            data->features.temperature_c,
            data->features.temperature_valid
        );

        if (err != ESP_OK) {
            return err;
        }

        err = draw_right_temperature(
            data->features.temperature_c,
            data->features.temperature_valid
        );

        if (err != ESP_OK) {
            return err;
        }

        s_cache.temperature_x10 =
            temperature_x10;

        s_cache.temperature_valid =
            data->features.temperature_valid;
    }

    /*
     * Kurtosis.
     */
    if (kurtosis_x100 != s_cache.kurtosis_x100 ||
        kurtosis_z_x10 != s_cache.kurtosis_z_x10 ||
        data->diagnostics.kurtosis_abnormal !=
            s_cache.kurtosis_abnormal) {

        err = draw_kurtosis(
            data->features.kurtosis,
            data->diagnostics.kurtosis_zscore,
            data->diagnostics.kurtosis_abnormal
        );

        if (err != ESP_OK) {
            return err;
        }

        s_cache.kurtosis_x100 =
            kurtosis_x100;

        s_cache.kurtosis_z_x10 =
            kurtosis_z_x10;

        s_cache.kurtosis_abnormal =
            data->diagnostics.kurtosis_abnormal;
    }

    /*
     * RMS.
     */
    if (rms_x10 != s_cache.rms_x10 ||
        rms_z_x10 != s_cache.rms_z_x10 ||
        data->diagnostics.rms_abnormal !=
            s_cache.rms_abnormal) {

        err = draw_rms(
            data->features.rms,
            data->diagnostics.rms_zscore,
            data->diagnostics.rms_abnormal
        );

        if (err != ESP_OK) {
            return err;
        }

        s_cache.rms_x10 =
            rms_x10;

        s_cache.rms_z_x10 =
            rms_z_x10;

        s_cache.rms_abnormal =
            data->diagnostics.rms_abnormal;
    }

    /*
     * 1x RPM.
     */
    if (rpm_amplitude_x10 !=
            s_cache.rpm_amplitude_x10 ||
        rpm_z_x10 != s_cache.rpm_z_x10 ||
        data->diagnostics.bin_1xrpm_abnormal !=
            s_cache.rpm_abnormal) {

        err = draw_1x_rpm(
            data->features.bin_1xrpm_amplitude,
            data->diagnostics.bin_1xrpm_zscore,
            data->diagnostics.bin_1xrpm_abnormal
        );

        if (err != ESP_OK) {
            return err;
        }

        s_cache.rpm_amplitude_x10 =
            rpm_amplitude_x10;

        s_cache.rpm_z_x10 =
            rpm_z_x10;

        s_cache.rpm_abnormal =
            data->diagnostics.bin_1xrpm_abnormal;
    }

    /*
     * Machine state.
     */
    if (data->state.state != s_cache.state) {

        err = draw_state(
            data->state.state
        );

        if (err != ESP_OK) {
            return err;
        }

        s_cache.state =
            data->state.state;
    }

    return ESP_OK;
}