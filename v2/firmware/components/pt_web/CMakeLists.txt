set(PT_WEB_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../web)
set(PT_WEB_OUT_DIR ${CMAKE_BINARY_DIR}/web)
set(PT_WEB_FILES index.html app.js style.css favicon.svg)

set(PT_WEB_SOURCES "")
set(PT_WEB_EMBEDDED "")
foreach(name ${PT_WEB_FILES})
    list(APPEND PT_WEB_SOURCES ${PT_WEB_SRC_DIR}/${name})
    list(APPEND PT_WEB_EMBEDDED ${PT_WEB_OUT_DIR}/${name}.gz)
endforeach()

execute_process(
    COMMAND sh ${PT_WEB_SRC_DIR}/pack.sh ${PT_WEB_OUT_DIR}
    RESULT_VARIABLE PT_WEB_PACK_RESULT)
if(NOT PT_WEB_PACK_RESULT EQUAL 0)
    message(FATAL_ERROR "pt_web: pack.sh failed")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PT_WEB_SOURCES} ${PT_WEB_SRC_DIR}/pack.sh)

idf_component_register(
    SRCS pt_web.c
    INCLUDE_DIRS include
    EMBED_FILES ${PT_WEB_EMBEDDED})
