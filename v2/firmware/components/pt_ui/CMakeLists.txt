idf_component_register(
    SRCS pt_ui.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc freertos esp_timer log)
