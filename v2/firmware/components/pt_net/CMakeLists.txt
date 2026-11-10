idf_component_register(
    SRCS pt_shell.c pt_store.c pt_wifi.c pt_improv.c pt_http.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES pt_web esp_http_server nvs_flash esp_wifi esp_netif esp_event esp_driver_uart esp_timer lwip)
