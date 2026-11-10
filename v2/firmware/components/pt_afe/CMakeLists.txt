idf_component_register(
    SRCS pt_afe.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES espressif__esp-sr freertos heap log)
