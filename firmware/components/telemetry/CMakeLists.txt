idf_component_register(
    SRCS "telemetry.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        app_context
        esp_wifi
        esp_event
        esp_netif
        mqtt
        nvs_flash
        esp-tls
)