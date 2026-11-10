idf_component_register(
    SRCS pt_ws.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES espressif__esp_websocket_client esp-tls mbedtls esp_timer freertos heap log)
