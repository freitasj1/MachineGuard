idf_component_register(
    SRCS "status.c" "hmi.c" "fft.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_context esp_driver_gpio esp_driver_spi
)