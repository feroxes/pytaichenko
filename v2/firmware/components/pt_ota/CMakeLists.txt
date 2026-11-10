idf_component_register(
    SRCS pt_ota.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES pt_net esp_http_client esp_https_ota app_update esp_app_format esp-tls mbedtls esp_timer esp_system
                  esp_partition)
