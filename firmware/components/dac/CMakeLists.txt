idf_component_register(
    SRCS "dac.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_context esp_driver_i2c
)