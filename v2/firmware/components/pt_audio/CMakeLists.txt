idf_component_register(
    SRCS pt_audio.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio freertos heap esp_timer log
    EMBED_FILES sounds/wake.raw sounds/end.raw)
