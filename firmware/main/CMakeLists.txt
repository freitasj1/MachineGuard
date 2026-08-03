idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_context accelerometer dsp_pipeline hmi storage sensors dac
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c
)
