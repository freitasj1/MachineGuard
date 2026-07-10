idf_component_register(
    SRCS "accelerometer.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_context
    REQUIRES driver esp_timer esp_driver_spi
)