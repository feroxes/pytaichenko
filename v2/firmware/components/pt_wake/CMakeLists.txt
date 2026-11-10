idf_component_register(
    SRCS pt_wake.cc pt_wake_engine.cc pt_features.c
    INCLUDE_DIRS include
    REQUIRES pt_core
    PRIV_REQUIRES espressif__esp-tflite-micro freertos heap log)

idf_component_get_property(tflm_dir espressif__esp-tflite-micro COMPONENT_DIR)
set(kissfft_srcs ${tflm_dir}/third_party/kissfft/kiss_fft.c ${tflm_dir}/third_party/kissfft/tools/kiss_fftr.c)
target_sources(${COMPONENT_LIB} PRIVATE ${kissfft_srcs})
set_source_files_properties(pt_features.c ${kissfft_srcs} PROPERTIES
    COMPILE_DEFINITIONS FIXED_POINT=16
    COMPILE_OPTIONS -ffp-contract=off)

set(models_dir ${CMAKE_CURRENT_LIST_DIR}/../../models)
foreach(model hey_jarvis okay_nabu hey_mycroft vad)
    target_add_binary_data(${COMPONENT_LIB} ${models_dir}/${model}.tflite BINARY ALIGN 16)
endforeach()
