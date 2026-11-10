if(ESP_PLATFORM)
    file(GLOB PT_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
    idf_component_register(
        SRCS ${PT_SRCS}
        INCLUDE_DIRS include
        REQUIRES json mbedtls)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PT_VERSION="${PROJECT_VER}")
endif()
