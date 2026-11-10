idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES pt_core pt_net pt_ws pt_app pt_ota esp_hw_support)
