idf_component_register(
    SRCS "system.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_context telemetry esp_driver_gpio
)