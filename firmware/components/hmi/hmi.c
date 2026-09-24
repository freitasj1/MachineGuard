/**

* @file hmi.c
* @brief Interface gráfica e gerenciamento da HMI.
  */

#include "app_context.h"
#include "esp_attr.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "portmacro.h"
#include "status.h"
#include "esp_system.h"

#include "hmi.h"
#include "status.h"
#include "fft.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"


static const char *TAG = "hmi";

/* ========================================================================== */
/* Display configuration                                                      */
/* ========================================================================== */

#define HMI_SPI_HOST        SPI3_HOST

#define HMI_LCD_WIDTH       480U
#define HMI_LCD_HEIGHT      320U

#define HMI_PIN_CS          48 
#define HMI_PIN_DC          47
#define HMI_PIN_RST         21

#define HMI_PIN_BUTTON      42

#define HMI_SPI_CLOCK_HZ    (20 * 1000 * 1000)

#define HMI_BYTES_PER_PIXEL 3U

#define HMI_REFRESH_PERIOD_MS 5000U

#define HMI_BUTTON_DEBOUNCE_MS 150U

#define HMI_BUTTON_LONG_PRESS_MS 7000U

/* ========================================================================== */
/* ILI9488 commands                                                           */
/* ========================================================================== */

#define ILI9488_SWRESET     0x01
#define ILI9488_SLPOUT      0x11
#define ILI9488_DISPON      0x29
#define ILI9488_CASET       0x2A
#define ILI9488_PASET       0x2B
#define ILI9488_RAMWR       0x2C
#define ILI9488_MADCTL      0x36
#define ILI9488_PIXFMT      0x3A

/* ========================================================================== */
/* Colors                                                                     */
/* ========================================================================== */

const hmi_color_t HMI_COLOR_BLACK =
{
.r = 3,
.g = 7,
.b = 11
};

const hmi_color_t HMI_COLOR_PANEL =
{
.r = 8,
.g = 15,
.b = 21
};

const hmi_color_t HMI_COLOR_WHITE =
{
.r = 235,
.g = 235,
.b = 235
};

const hmi_color_t HMI_COLOR_GRAY =
{
.r = 125,
.g = 135,
.b = 145
};

const hmi_color_t HMI_COLOR_DARK_GRAY =
{
.r = 45,
.g = 58,
.b = 68
};

const hmi_color_t HMI_COLOR_BLUE =
{
.r = 0,
.g = 145,
.b = 210
};

const hmi_color_t HMI_COLOR_GREEN =
{
.r = 0,
.g = 240,
.b = 75
};

const hmi_color_t HMI_COLOR_RED =
{
.r = 255,
.g = 55,
.b = 55
};

const hmi_color_t HMI_COLOR_YELLOW =
{
.r = 245,
.g = 210,
.b = 0
};

/* ========================================================================== */
/* Private state                                                              */
/* ========================================================================== */

static spi_device_handle_t hmi_spi = NULL;

static hmi_screen_t current_screen = HMI_SCREEN_STATUS;

static volatile bool button_pressed = false;

static volatile uint32_t button_interrupt_count = 0;

static volatile TickType_t button_last_interrupt = 0;

static TickType_t button_press_start = 0;
/*

* One complete RGB666 row.
  */
  static uint8_t display_buffer[
  HMI_LCD_WIDTH * HMI_BYTES_PER_PIXEL
  ];

/*

* Maximum glyph:
*
* 5 x 7 pixels
* scale 3
* 15 x 21 x 3 = 945 bytes
*
* 2048 bytes gives enough margin.
  */
  static uint8_t glyph_buffer[2048];

  
/* ========================================================================== */
/* Private prototypes                                                         */
/* ========================================================================== */

static esp_err_t hmi_init(void);

static esp_err_t hmi_write_command(
uint8_t command
);

static esp_err_t hmi_write_data(
const uint8_t *data,
size_t length
);

static esp_err_t hmi_write_u8(
uint8_t value
);

static esp_err_t hmi_set_window(
uint16_t x0,
uint16_t y0,
uint16_t x1,
uint16_t y1
);

static esp_err_t hmi_draw_glyph(
uint16_t x,
uint16_t y,
char character,
hmi_font_scale_t scale,
hmi_color_t color
);

static const uint8_t *hmi_get_glyph(
char character
);

static esp_err_t hmi_update_screen(
const hmi_data_t *data
);

static esp_err_t hmi_switch_screen(
hmi_screen_t screen
);

static void  hmi_button_isr_handler(
void *arg
);

/* ========================================================================== */
/* Button interrupt                                                           */
/* ========================================================================== */


/**

* @brief Handles the HMI button interrupt.
*
* @param arg Unused interrupt argument.
  */
static void IRAM_ATTR hmi_button_isr_handler(void *arg)
{
    (void)arg;

    const TickType_t now = xTaskGetTickCountFromISR();

    if ((now - button_last_interrupt) < pdMS_TO_TICKS(HMI_BUTTON_DEBOUNCE_MS))
    {
        return;
    }

    button_last_interrupt = now;
    button_interrupt_count++;
    button_pressed = true;
}

/* ========================================================================== */
/* Public task                                                                */
/* ========================================================================== */

void task_hmi(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (ctx == NULL ||
        ctx->queue_system_to_hmi == NULL)
    {
        ESP_LOGE(TAG, "invalid HMI context");

        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting HMI task");

    esp_err_t err = hmi_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "HMI initialization failed: %s",
            esp_err_to_name(err)
        );

        vTaskDelete(NULL);
        return;
    }

    /*
     * STATUS is the default screen.
     */
    current_screen = HMI_SCREEN_STATUS;

    err = status_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "STATUS initialization failed: %s",
            esp_err_to_name(err)
        );

        vTaskDelete(NULL);
        return;
    }

    hmi_data_t last_data = {0};

    bool have_data = false;
    bool drawn_once = false;
    bool render_pending = false;

    TickType_t next_render = 0;

    while (true)
    {
        /*
         * Detect button press.
         */
        if (button_pressed)
        {
            button_pressed = false;

            button_press_start = xTaskGetTickCount();

            ESP_LOGI(
                TAG,
                "Button pressed | interrupts=%lu",
                (unsigned long)button_interrupt_count
            );
        }

        /*
         * Handle button while it remains pressed.
         */
        if (button_press_start != 0)
        {
            if (gpio_get_level(HMI_PIN_BUTTON) == 0)
            {
                TickType_t now = xTaskGetTickCount();

                /*
                 * Long press: restart ESP32-S3.
                 */
                if ((now - button_press_start) >=
                    pdMS_TO_TICKS(HMI_BUTTON_LONG_PRESS_MS))
                {
                    ESP_LOGW(
                        TAG,
                        "Long button press detected - restarting ESP32"
                    );

                    vTaskDelay(pdMS_TO_TICKS(100));

                    esp_restart();
                }
            }
            else
            {
                /*
                 * Button was released.
                 */
                TickType_t press_duration =
                    xTaskGetTickCount() - button_press_start;

                button_press_start = 0;

                /*
                 * Short press: switch screen.
                 */
                if (press_duration <
                    pdMS_TO_TICKS(HMI_BUTTON_LONG_PRESS_MS))
                {
                    hmi_screen_t next_screen;

                    if (current_screen == HMI_SCREEN_STATUS)
                    {
                        next_screen = HMI_SCREEN_FFT;
                    }
                    else
                    {
                        next_screen = HMI_SCREEN_STATUS;
                    }

                    err = hmi_switch_screen(next_screen);

                    if (err != ESP_OK)
                    {
                        ESP_LOGE(
                            TAG,
                            "screen switch failed: %s",
                            esp_err_to_name(err)
                        );
                    }
                    else
                    {
                        drawn_once = false;
                        render_pending = have_data;
                        next_render = 0;

                        ESP_LOGI(
                            TAG,
                            "screen changed to %s",
                            next_screen == HMI_SCREEN_STATUS
                                ? "STATUS"
                                : "FFT"
                        );
                    }
                }
            }
        }

        hmi_data_t rx = {0};

        if (xQueueReceive(
                ctx->queue_system_to_hmi,
                &rx,
                pdMS_TO_TICKS(200)) == pdTRUE)
        {
            last_data = rx;

            have_data = true;
            render_pending = true;
        }

        if (have_data && render_pending)
        {
            TickType_t now = xTaskGetTickCount();

            if (!drawn_once ||
                (int32_t)(now - next_render) >= 0)
            {
                err = hmi_update_screen(&last_data);

                if (err != ESP_OK)
                {
                    ESP_LOGE(
                        TAG,
                        "HMI update failed: %s",
                        esp_err_to_name(err)
                    );
                }
                else
                {
                    drawn_once = true;
                    render_pending = false;

                    next_render =
                        xTaskGetTickCount() +
                        pdMS_TO_TICKS(
                            HMI_REFRESH_PERIOD_MS
                        );
                }
            }
        }
    }
}

/* ========================================================================== */
/* Screen management                                                          */
/* ========================================================================== */
static esp_err_t hmi_switch_screen(hmi_screen_t screen)
{
    esp_err_t err;

    err = hmi_display_clear(HMI_COLOR_BG);

    if (err != ESP_OK) {
        return err;
    }

    switch (screen) {

        case HMI_SCREEN_STATUS:

            current_screen = HMI_SCREEN_STATUS;

            return status_init();

        case HMI_SCREEN_FFT:

            current_screen = HMI_SCREEN_FFT;

            return fft_init();

        default:

            return ESP_ERR_INVALID_ARG;
    }
}


static esp_err_t hmi_update_screen(
const hmi_data_t *data
)

{
if (data == NULL)
{
return ESP_ERR_INVALID_ARG;
}


switch (current_screen)
{

    case HMI_SCREEN_STATUS:
        return status_update(data);

    case HMI_SCREEN_FFT:
        return fft_update(data);

    default:

        current_screen = HMI_SCREEN_STATUS;

        return status_update(data);
}


}

/* ========================================================================== */
/* ILI9488 initialization                                                     */
/* ========================================================================== */

static esp_err_t hmi_init(void)

{
ESP_LOGI(TAG, "Initializing ILI9488 HMI");


/*
 * DC and RESET are owned by the HMI component.
 *
 * SPI3 itself is initialized by main.c.
 */
gpio_config_t io_conf =
{
    .pin_bit_mask =
        (1ULL << HMI_PIN_DC) |
        (1ULL << HMI_PIN_RST),

    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

ESP_RETURN_ON_ERROR(
    gpio_config(&io_conf),
    TAG,
    "failed to configure HMI GPIO"
);

gpio_set_level(HMI_PIN_DC, 1);
gpio_set_level(HMI_PIN_RST, 1);

spi_device_interface_config_t device_config =
{
    .clock_speed_hz = HMI_SPI_CLOCK_HZ,
    .mode = 0,
    .spics_io_num = HMI_PIN_CS,
    .queue_size = 4,
    .flags = SPI_DEVICE_HALFDUPLEX,
};

ESP_RETURN_ON_ERROR(
    spi_bus_add_device(
        HMI_SPI_HOST,
        &device_config,
        &hmi_spi
    ),
    TAG,
    "failed to add ILI9488 to SPI3"
);

ESP_LOGI(
    TAG,
    "ILI9488 SPI device added at %d Hz",
    HMI_SPI_CLOCK_HZ
);

/* Hardware reset. */
gpio_set_level(HMI_PIN_RST, 0);

vTaskDelay(pdMS_TO_TICKS(20));

gpio_set_level(HMI_PIN_RST, 1);

vTaskDelay(pdMS_TO_TICKS(120));

/* Software reset. */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_SWRESET),
    TAG,
    "SWRESET failed"
);

vTaskDelay(pdMS_TO_TICKS(120));

/* Exit sleep mode. */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_SLPOUT),
    TAG,
    "SLPOUT failed"
);

vTaskDelay(pdMS_TO_TICKS(120));

/* RGB666. */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_PIXFMT),
    TAG,
    "PIXFMT command failed"
);

ESP_RETURN_ON_ERROR(
    hmi_write_u8(0x66),
    TAG,
    "PIXFMT data failed"
);

/*
 * Landscape orientation.
 *
 * This is the orientation already validated with the display.
 */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_MADCTL),
    TAG,
    "MADCTL command failed"
);

ESP_RETURN_ON_ERROR(
    hmi_write_u8(0x28),
    TAG,
    "MADCTL data failed"
);

/* Display ON. */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_DISPON),
    TAG,
    "DISPON failed"
);

vTaskDelay(pdMS_TO_TICKS(20));

/*
 * HMI button.
 *
 * External 10 kOhm pull-up is used.
 * Button connects GPIO47 to GND.
 */
gpio_config_t button_config =
{
    .pin_bit_mask = (1ULL << HMI_PIN_BUTTON),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
};

ESP_RETURN_ON_ERROR(
    gpio_config(&button_config),
    TAG,
    "failed to configure HMI button"
);

ESP_RETURN_ON_ERROR(
    gpio_install_isr_service(0),
    TAG,
    "failed to install GPIO ISR service"
);

ESP_RETURN_ON_ERROR(
    gpio_isr_handler_add(
        HMI_PIN_BUTTON,
        hmi_button_isr_handler,
        NULL
    ),
    TAG,
    "failed to add HMI button ISR"
);

ESP_LOGI(
    TAG,
    "HMI button configured on GPIO%d",
    HMI_PIN_BUTTON
);

ESP_LOGI(TAG, "ILI9488 initialized");

return ESP_OK;


}

/* ========================================================================== */
/* Low-level display                                                          */
/* ========================================================================== */

static esp_err_t hmi_write_command(
uint8_t command
)

{
if (hmi_spi == NULL)
{
return ESP_ERR_INVALID_STATE;
}


spi_transaction_t transaction =
{
    .length = 8,
    .tx_buffer = &command,
};

gpio_set_level(HMI_PIN_DC, 0);

return spi_device_transmit(
    hmi_spi,
    &transaction
);


}

static esp_err_t hmi_write_data(const uint8_t *data, size_t length)
{
if (data == NULL || length == 0U)
{
return ESP_OK;
}


if (hmi_spi == NULL)
{
    return ESP_ERR_INVALID_STATE;
}

spi_transaction_t transaction =
{
    .length = length * 8U,
    .tx_buffer = data,
};

gpio_set_level(HMI_PIN_DC, 1);

return spi_device_transmit(
    hmi_spi,
    &transaction
);


}

static esp_err_t hmi_write_u8(
uint8_t value
)

{
return hmi_write_data(
&value,
1U
);
}

static esp_err_t hmi_set_window(
uint16_t x0,
uint16_t y0,
uint16_t x1,
uint16_t y1
)

{
uint8_t data[4];


if (x0 > x1 ||
    y0 > y1 ||
    x1 >= HMI_LCD_WIDTH ||
    y1 >= HMI_LCD_HEIGHT)
{

    return ESP_ERR_INVALID_ARG;
}

/* CASET */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_CASET),
    TAG,
    "CASET failed"
);

data[0] = (uint8_t)(x0 >> 8);
data[1] = (uint8_t)(x0 & 0xFF);
data[2] = (uint8_t)(x1 >> 8);
data[3] = (uint8_t)(x1 & 0xFF);

ESP_RETURN_ON_ERROR(
    hmi_write_data(
        data,
        sizeof(data)
    ),
    TAG,
    "CASET data failed"
);

/* PASET */
ESP_RETURN_ON_ERROR(
    hmi_write_command(ILI9488_PASET),
    TAG,
    "PASET failed"
);

data[0] = (uint8_t)(y0 >> 8);
data[1] = (uint8_t)(y0 & 0xFF);
data[2] = (uint8_t)(y1 >> 8);
data[3] = (uint8_t)(y1 & 0xFF);

ESP_RETURN_ON_ERROR(
    hmi_write_data(
        data,
        sizeof(data)
    ),
    TAG,
    "PASET data failed"
);

return hmi_write_command(
    ILI9488_RAMWR
);


}

/* ========================================================================== */
/* Public drawing API                                                         */
/* ========================================================================== */

esp_err_t hmi_display_clear(
hmi_color_t color
)

{
return hmi_display_fill_rect(
0,
0,
HMI_LCD_WIDTH,
HMI_LCD_HEIGHT,
color
);
}

esp_err_t hmi_display_fill_rect(
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, hmi_color_t color) 
{
if (width == 0U || height == 0U)
{
return ESP_OK;
}


if (x >= HMI_LCD_WIDTH ||
    y >= HMI_LCD_HEIGHT)
{

    return ESP_ERR_INVALID_ARG;
}

if ((uint32_t)x + width > HMI_LCD_WIDTH)
{
    width = HMI_LCD_WIDTH - x;
}

if ((uint32_t)y + height > HMI_LCD_HEIGHT)
{
    height = HMI_LCD_HEIGHT - y;
}

ESP_RETURN_ON_ERROR(
    hmi_set_window(
        x,
        y,
        x + width - 1U,
        y + height - 1U
    ),
    TAG,
    "failed to set rectangle window"
);

for (uint16_t column = 0U;
     column < width;
     ++column)
{

    display_buffer[
        column * HMI_BYTES_PER_PIXEL + 0U
    ] = color.r;

    display_buffer[
        column * HMI_BYTES_PER_PIXEL + 1U
    ] = color.g;

    display_buffer[
        column * HMI_BYTES_PER_PIXEL + 2U
    ] = color.b;
}

for (uint16_t row = 0U;
     row < height;
     ++row)
{

    ESP_RETURN_ON_ERROR(
        hmi_write_data(
            display_buffer,
            (size_t)width *
                HMI_BYTES_PER_PIXEL
        ),
        TAG,
        "failed to write rectangle"
    );
}

return ESP_OK;


}

esp_err_t hmi_display_draw_hline(
uint16_t x,
uint16_t y,
uint16_t width,
hmi_color_t color
)

{
return hmi_display_fill_rect(
x,
y,
width,
1U,
color
);
}

esp_err_t hmi_display_draw_vline(
uint16_t x,
uint16_t y,
uint16_t height,
hmi_color_t color
)

{
return hmi_display_fill_rect(
x,
y,
1U,
height,
color
);
}

/* ========================================================================== */
/* 5x7 font                                                                   */
/* ========================================================================== */

static const uint8_t GLYPH_SPACE[5] =
{
0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t GLYPH_0[5] =
{
0x3E, 0x51, 0x49, 0x45, 0x3E
};

static const uint8_t GLYPH_1[5] =
{
0x00, 0x42, 0x7F, 0x40, 0x00
};

static const uint8_t GLYPH_2[5] =
{
0x42, 0x61, 0x51, 0x49, 0x46
};

static const uint8_t GLYPH_3[5] =
{
0x21, 0x41, 0x45, 0x4B, 0x31
};

static const uint8_t GLYPH_4[5] =
{
0x18, 0x14, 0x12, 0x7F, 0x10
};

static const uint8_t GLYPH_5[5] =
{
0x27, 0x45, 0x45, 0x45, 0x39
};

static const uint8_t GLYPH_6[5] =
{
0x3C, 0x4A, 0x49, 0x49, 0x30
};

static const uint8_t GLYPH_7[5] =
{
0x01, 0x71, 0x09, 0x05, 0x03
};

static const uint8_t GLYPH_8[5] =
{
0x36, 0x49, 0x49, 0x49, 0x36
};

static const uint8_t GLYPH_9[5] =
{
0x06, 0x49, 0x49, 0x29, 0x1E
};

static const uint8_t GLYPH_A[5] =
{
0x7E, 0x11, 0x11, 0x11, 0x7E
};

static const uint8_t GLYPH_B[5] =
{
0x7F, 0x49, 0x49, 0x49, 0x36
};

static const uint8_t GLYPH_C[5] =
{
0x3E, 0x41, 0x41, 0x41, 0x22
};

static const uint8_t GLYPH_D[5] =
{
0x7F, 0x41, 0x41, 0x22, 0x1C
};

static const uint8_t GLYPH_E[5] =
{
0x7F, 0x49, 0x49, 0x49, 0x41
};

static const uint8_t GLYPH_F[5] =
{
0x7F, 0x09, 0x09, 0x09, 0x01
};

static const uint8_t GLYPH_G[5] =
{
0x3E, 0x41, 0x49, 0x49, 0x7A
};

static const uint8_t GLYPH_H[5] =
{
0x7F, 0x08, 0x08, 0x08, 0x7F
};

static const uint8_t GLYPH_I[5] =
{
0x00, 0x41, 0x7F, 0x41, 0x00
};

static const uint8_t GLYPH_J[5] =
{
0x20, 0x40, 0x41, 0x3F, 0x01
};

static const uint8_t GLYPH_K[5] =
{
0x7F, 0x08, 0x14, 0x22, 0x41
};

static const uint8_t GLYPH_L[5] =
{
0x7F, 0x40, 0x40, 0x40, 0x40
};

static const uint8_t GLYPH_M[5] =
{
0x7F, 0x02, 0x0C, 0x02, 0x7F
};

static const uint8_t GLYPH_N[5] =
{
0x7F, 0x04, 0x08, 0x10, 0x7F
};

static const uint8_t GLYPH_O[5] =
{
0x3E, 0x41, 0x41, 0x41, 0x3E
};

static const uint8_t GLYPH_P[5] =
{
0x7F, 0x09, 0x09, 0x09, 0x06
};

static const uint8_t GLYPH_Q[5] =
{
0x3E, 0x41, 0x51, 0x21, 0x5E
};

static const uint8_t GLYPH_R[5] =
{
0x7F, 0x09, 0x19, 0x29, 0x46
};

static const uint8_t GLYPH_S[5] =
{
0x46, 0x49, 0x49, 0x49, 0x31
};

static const uint8_t GLYPH_T[5] =
{
0x01, 0x01, 0x7F, 0x01, 0x01
};

static const uint8_t GLYPH_U[5] =
{
0x3F, 0x40, 0x40, 0x40, 0x3F
};

static const uint8_t GLYPH_V[5] =
{
0x1F, 0x20, 0x40, 0x20, 0x1F
};

static const uint8_t GLYPH_W[5] =
{
0x7F, 0x20, 0x18, 0x20, 0x7F
};

static const uint8_t GLYPH_X[5] =
{
0x63, 0x14, 0x08, 0x14, 0x63
};

static const uint8_t GLYPH_Y[5] =
{
0x07, 0x08, 0x70, 0x08, 0x07
};

static const uint8_t GLYPH_Z[5] =
{
0x61, 0x51, 0x49, 0x45, 0x43
};

static const uint8_t GLYPH_DOT[5] =
{
0x00, 0x60, 0x60, 0x00, 0x00
};

static const uint8_t GLYPH_COLON[5] =
{
0x00, 0x36, 0x36, 0x00, 0x00
};

static const uint8_t GLYPH_MINUS[5] =
{
0x08, 0x08, 0x08, 0x08, 0x08
};

static const uint8_t GLYPH_PERCENT[5] =
{
0x63, 0x13, 0x08, 0x64, 0x63
};

static const uint8_t GLYPH_SLASH[5] =
{
0x40, 0x20, 0x10, 0x08, 0x04
};

static const uint8_t GLYPH_EQUAL[5] =
{
0x14, 0x14, 0x14, 0x14, 0x14
};

static const uint8_t GLYPH_LEFT_BRACKET[5] =
{
0x7F, 0x41, 0x41, 0x00, 0x00
};

static const uint8_t GLYPH_RIGHT_BRACKET[5] =
{
0x00, 0x00, 0x41, 0x41, 0x7F
};

static const uint8_t *hmi_get_glyph(
char character
)

{
switch (character)
{


    case ' ': return GLYPH_SPACE;

    case '0': return GLYPH_0;
    case '1': return GLYPH_1;
    case '2': return GLYPH_2;
    case '3': return GLYPH_3;
    case '4': return GLYPH_4;
    case '5': return GLYPH_5;
    case '6': return GLYPH_6;
    case '7': return GLYPH_7;
    case '8': return GLYPH_8;
    case '9': return GLYPH_9;

    case 'A': return GLYPH_A;
    case 'B': return GLYPH_B;
    case 'C': return GLYPH_C;
    case 'D': return GLYPH_D;
    case 'E': return GLYPH_E;
    case 'F': return GLYPH_F;
    case 'G': return GLYPH_G;
    case 'H': return GLYPH_H;
    case 'I': return GLYPH_I;
    case 'J': return GLYPH_J;
    case 'K': return GLYPH_K;
    case 'L': return GLYPH_L;
    case 'M': return GLYPH_M;
    case 'N': return GLYPH_N;
    case 'O': return GLYPH_O;
    case 'P': return GLYPH_P;
    case 'Q': return GLYPH_Q;
    case 'R': return GLYPH_R;
    case 'S': return GLYPH_S;
    case 'T': return GLYPH_T;
    case 'U': return GLYPH_U;
    case 'V': return GLYPH_V;
    case 'W': return GLYPH_W;
    case 'X': return GLYPH_X;
    case 'Y': return GLYPH_Y;
    case 'Z': return GLYPH_Z;

    case '.': return GLYPH_DOT;
    case ':': return GLYPH_COLON;
    case '-': return GLYPH_MINUS;
    case '%': return GLYPH_PERCENT;
    case '/': return GLYPH_SLASH;
    case '=': return GLYPH_EQUAL;
    case '[': return GLYPH_LEFT_BRACKET;
    case ']': return GLYPH_RIGHT_BRACKET;

    default:
        return GLYPH_SPACE;
}


}

/* ========================================================================== */
/* Glyph rendering                                                            */
/* ========================================================================== */

static esp_err_t hmi_draw_glyph(uint16_t x, uint16_t y, char character, 
    hmi_font_scale_t scale, hmi_color_t color)

{
if (scale == 0U)
{
return ESP_ERR_INVALID_ARG;
}


const uint8_t *glyph =
    hmi_get_glyph(character);

const uint16_t width =
    5U * (uint16_t)scale;

const uint16_t height =
    7U * (uint16_t)scale;

const size_t required_size =
    (size_t)width *
    height *
    HMI_BYTES_PER_PIXEL;

if (required_size > sizeof(glyph_buffer))
{
    return ESP_ERR_INVALID_SIZE;
}

if (x >= HMI_LCD_WIDTH ||
    y >= HMI_LCD_HEIGHT)
{

    return ESP_ERR_INVALID_ARG;
}

if ((uint32_t)x + width > HMI_LCD_WIDTH ||
    (uint32_t)y + height > HMI_LCD_HEIGHT)
{

    return ESP_ERR_INVALID_ARG;
}

size_t index = 0U;

for (uint8_t row = 0U;
     row < 7U;
     ++row)
{

    for (uint8_t sy = 0U;
         sy < (uint8_t)scale;
         ++sy)
{

        for (uint8_t column = 0U;
             column < 5U;
             ++column)
{

            const bool pixel =
                (glyph[column] &
                 (1U << row)) != 0U;

            for (uint8_t sx = 0U;
                 sx < (uint8_t)scale;
                 ++sx)
{

                if (pixel)
{

                    glyph_buffer[index + 0U] =
                        color.r;

                    glyph_buffer[index + 1U] =
                        color.g;

                    glyph_buffer[index + 2U] =
                        color.b;

                } else
{

                    glyph_buffer[index + 0U] =
                        HMI_COLOR_BLACK.r;

                    glyph_buffer[index + 1U] =
                        HMI_COLOR_BLACK.g;

                    glyph_buffer[index + 2U] =
                        HMI_COLOR_BLACK.b;
                }

                index += 3U;
            }
        }
    }
}

ESP_RETURN_ON_ERROR(
    hmi_set_window(
        x,
        y,
        x + width - 1U,
        y + height - 1U
    ),
    TAG,
    "failed to set glyph window"
);

return hmi_write_data(
    glyph_buffer,
    required_size
);


}

esp_err_t hmi_display_draw_text(
uint16_t x,
uint16_t y,
const char *text,
hmi_font_scale_t scale,
hmi_color_t color
)

{
if (text == NULL || scale == 0U)
{
return ESP_ERR_INVALID_ARG;
}


uint16_t cursor_x = x;

while (*text!='\0')
{

    if (*text==' ')
{

        cursor_x +=
            6U * (uint16_t)scale;

        ++text;

        continue;
    }

    ESP_RETURN_ON_ERROR(
        hmi_draw_glyph(
            cursor_x,
            y,
            *text,
            scale,
            color
        ),
        TAG,
        "failed to draw glyph"
    );

    cursor_x +=
        6U * (uint16_t)scale;

    ++text;
}

return ESP_OK;


}

/*========================================================================== */
/* Degree symbol                                                              */
/* ========================================================================== */

esp_err_t hmi_display_draw_degree_symbol(
uint16_t x,
uint16_t y,
hmi_color_t color
)

{
/*
* Draw a small degree symbol using primitives instead of
* depending on UTF-8/extended character encoding.
*/
esp_err_t err;


err = hmi_display_fill_rect(
    x + 2U,
    y,
    3U,
    1U,
    color
);

if (err != ESP_OK)
{
    return err;
}

err = hmi_display_fill_rect(
    x + 1U,
    y + 1U,
    1U,
    3U,
    color
);

if (err != ESP_OK)
{
    return err;
}

err = hmi_display_fill_rect(
    x + 5U,
    y + 1U,
    1U,
    3U,
    color
);

if (err != ESP_OK)
{
    return err;
}

return hmi_display_fill_rect(
    x + 2U,
    y + 4U,
    3U,
    1U,
    color
);


}
