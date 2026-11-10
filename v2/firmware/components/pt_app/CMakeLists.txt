idf_component_register(
    SRCS pt_app.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES pt_audio pt_afe pt_ui pt_wake pt_ws pt_net freertos esp_timer heap log)
