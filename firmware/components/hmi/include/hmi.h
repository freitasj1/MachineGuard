/**
 * @file hmi.h
 * @brief Interface gráfica e gerenciamento da HMI.
 */

#pragma once

#include <stdint.h>

#include "app_context.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Screen                                                                     */
/* ========================================================================== */

typedef enum {
    HMI_SCREEN_STATUS = 0,
    HMI_SCREEN_FFT
} hmi_screen_t;

/* ========================================================================== */
/* Color                                                                      */
/* ========================================================================== */

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} hmi_color_t;

extern const hmi_color_t HMI_COLOR_BLACK;
extern const hmi_color_t HMI_COLOR_PANEL;
extern const hmi_color_t HMI_COLOR_WHITE;
extern const hmi_color_t HMI_COLOR_GRAY;
extern const hmi_color_t HMI_COLOR_DARK_GRAY;
extern const hmi_color_t HMI_COLOR_BLUE;
extern const hmi_color_t HMI_COLOR_GREEN;
extern const hmi_color_t HMI_COLOR_RED;
extern const hmi_color_t HMI_COLOR_YELLOW;

/*
 * Semantic aliases used by screen modules.
 */
#define HMI_COLOR_BG      HMI_COLOR_BLACK
#define HMI_COLOR_HEADER  HMI_COLOR_BLUE
#define HMI_COLOR_LINE    HMI_COLOR_DARK_GRAY

/* ========================================================================== */
/* Font                                                                       */
/* ========================================================================== */

typedef enum {
    HMI_FONT_SMALL = 1,
    HMI_FONT_MEDIUM = 2,
    HMI_FONT_LARGE = 3
} hmi_font_scale_t;

/* ========================================================================== */
/* HMI task                                                                  */
/* ========================================================================== */

/**
 * @brief HMI FreeRTOS task.
 *
 * @param arg Pointer to app_context_t.
 */
void task_hmi(void *arg);

/* ========================================================================== */
/* Display API                                                               */
/* ========================================================================== */

/**
 * @brief Clear the complete display.
 */
esp_err_t hmi_display_clear(
    hmi_color_t color
);

/**
 * @brief Fill a rectangular region.
 */
esp_err_t hmi_display_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    hmi_color_t color
);

/**
 * @brief Draw horizontal line.
 */
esp_err_t hmi_display_draw_hline(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    hmi_color_t color
);

/**
 * @brief Draw vertical line.
 */
esp_err_t hmi_display_draw_vline(
    uint16_t x,
    uint16_t y,
    uint16_t height,
    hmi_color_t color
);

/**
 * @brief Draw text using the internal 5x7 font.
 */
esp_err_t hmi_display_draw_text(
    uint16_t x,
    uint16_t y,
    const char *text,
    hmi_font_scale_t scale,
    hmi_color_t color
);

/**
 * @brief Draw a degree symbol.
 */
esp_err_t hmi_display_draw_degree_symbol(
    uint16_t x,
    uint16_t y,
    hmi_color_t color
);

#ifdef __cplusplus
}
#endif