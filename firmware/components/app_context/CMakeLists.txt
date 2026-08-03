idf_component_register(
    SRCS "app_context.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c
)