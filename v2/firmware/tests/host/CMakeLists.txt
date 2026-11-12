cmake_minimum_required(VERSION 3.20)
project(pt_core_host C CXX)

include(CTest)
include(FetchContent)
include(CheckCSourceRuns)

option(PT_REQUIRE_SANITIZERS "Fail when address and undefined sanitizers do not run on this host" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR sources-only)
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR sources-only)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
FetchContent_Declare(tflm
    URL https://github.com/espressif/esp-tflite-micro/archive/5d505b5ffac8e60923bf29a0cc6e6348c84e3763.tar.gz
    URL_HASH SHA256=153b6db0c5d9d8d3160f926c94b52d75d902b35357a97677177de078aa003708
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR sources-only)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_AS_SUBPROJECT ON)

FetchContent_MakeAvailable(unity cjson mbedtls tflm)

add_library(unity_lib STATIC ${unity_SOURCE_DIR}/src/unity.c)
target_include_directories(unity_lib PUBLIC ${unity_SOURCE_DIR}/src)

add_library(cjson_lib STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson_lib PUBLIC ${cjson_SOURCE_DIR})

set(PT_CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/pt_core)
set(PT_WAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/pt_wake)

set(KISSFFT_DIR ${tflm_SOURCE_DIR}/third_party/kissfft)
add_library(kissfft_lib STATIC ${KISSFFT_DIR}/kiss_fft.c ${KISSFFT_DIR}/tools/kiss_fftr.c)
target_include_directories(kissfft_lib PUBLIC ${KISSFFT_DIR})
target_compile_definitions(kissfft_lib PUBLIC FIXED_POINT=16)
target_compile_options(kissfft_lib PRIVATE -ffp-contract=off)
set_source_files_properties(${PT_WAKE_DIR}/pt_features.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)

set(TFLM_DIR ${tflm_SOURCE_DIR}/tensorflow/lite)
file(GLOB TFLM_SRCS
    ${TFLM_DIR}/micro/*.cc
    ${TFLM_DIR}/micro/kernels/*.cc
    ${TFLM_DIR}/micro/tflite_bridge/*.cc
    ${TFLM_DIR}/micro/arena_allocator/*.cc
    ${TFLM_DIR}/micro/memory_planner/*.cc)
list(FILTER TFLM_SRCS EXCLUDE REGEX "(test_helper|fake_micro_context|mock_micro_graph|hexdump|_test)[^/]*\\.cc$")
add_library(tflm_lib STATIC ${TFLM_SRCS}
    ${TFLM_DIR}/kernels/kernel_util.cc
    ${TFLM_DIR}/core/c/common.cc
    ${TFLM_DIR}/core/api/flatbuffer_conversions.cc
    ${TFLM_DIR}/core/api/tensor_utils.cc
    ${TFLM_DIR}/kernels/internal/common.cc
    ${TFLM_DIR}/kernels/internal/quantization_util.cc
    ${TFLM_DIR}/kernels/internal/portable_tensor_utils.cc
    ${TFLM_DIR}/kernels/internal/tensor_utils.cc
    ${TFLM_DIR}/kernels/internal/tensor_ctypes.cc
    ${TFLM_DIR}/kernels/internal/reference/portable_tensor_utils.cc
    ${tflm_SOURCE_DIR}/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc
    ${tflm_SOURCE_DIR}/tensorflow/compiler/mlir/lite/schema/schema_utils.cc)
target_include_directories(tflm_lib SYSTEM PUBLIC
    ${tflm_SOURCE_DIR}
    ${tflm_SOURCE_DIR}/third_party/gemmlowp
    ${tflm_SOURCE_DIR}/third_party/flatbuffers/include
    ${tflm_SOURCE_DIR}/third_party/ruy)
target_compile_definitions(tflm_lib PUBLIC TF_LITE_STATIC_MEMORY TF_LITE_DISABLE_X86_NEON)
target_compile_options(tflm_lib PRIVATE -w -O2 -fno-rtti -fno-exceptions)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../VERSION)
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../../VERSION PT_VERSION_TEXT LIMIT_COUNT 1)

file(GLOB PT_CORE_SRCS CONFIGURE_DEPENDS ${PT_CORE_DIR}/src/*.c)

set(PT_SANITIZE_SRC "int main(void) { return 0; }")
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
check_c_source_runs("${PT_SANITIZE_SRC}" PT_HAVE_ASAN_UBSAN)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
check_c_source_runs("${PT_SANITIZE_SRC}" PT_HAVE_UBSAN)
unset(CMAKE_REQUIRED_FLAGS)

if(PT_HAVE_ASAN_UBSAN)
    set(PT_SANITIZE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
elseif(PT_REQUIRE_SANITIZERS)
    message(FATAL_ERROR "address and undefined sanitizers do not run on this host")
elseif(PT_HAVE_UBSAN)
    message(WARNING "AddressSanitizer does not run on this host, using UndefinedBehaviorSanitizer only")
    set(PT_SANITIZE -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
else()
    message(WARNING "no sanitizers run on this host")
    set(PT_SANITIZE "")
endif()
set(PT_WARN -Wall -Wextra -Werror)

function(pt_add_test name)
    add_executable(test_${name} test_${name}.c ${ARGN} ${PT_CORE_SRCS})
    target_include_directories(test_${name} PRIVATE
        ${PT_CORE_DIR}/include
        ${PT_CORE_DIR}/src)
    target_compile_options(test_${name} PRIVATE ${PT_WARN} ${PT_SANITIZE})
    target_link_options(test_${name} PRIVATE ${PT_SANITIZE})
    target_compile_definitions(test_${name} PRIVATE _POSIX_C_SOURCE=200809L PT_VERSION="${PT_VERSION_TEXT}")
    target_link_libraries(test_${name} PRIVATE unity_lib cjson_lib mbedcrypto)
    if(NOT APPLE)
        target_link_libraries(test_${name} PRIVATE m)
    endif()
    add_test(NAME ${name} COMMAND test_${name})
    set(env UBSAN_OPTIONS=print_stacktrace=1)
    if(APPLE)
        list(APPEND env ASAN_OPTIONS=detect_leaks=0)
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
endfunction()

pt_add_test(core)
pt_add_test(settings)
pt_add_test(auth)
pt_add_test(log)
pt_add_test(api)
pt_add_test(otapick)
pt_add_test(improv)
pt_add_test(dsp)
target_compile_definitions(test_dsp PRIVATE PT_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
pt_add_test(tools)
pt_add_test(rt)
pt_add_test(session fake_port.c)
pt_add_test(turn fake_port.c)
pt_add_test(live fake_port.c)
pt_add_test(pcm)
pt_add_test(ring)
pt_add_test(chan)
pt_add_test(wsurl)
pt_add_test(appstate)
pt_add_test(features ${PT_WAKE_DIR}/pt_features.c)
target_include_directories(test_features PRIVATE ${PT_WAKE_DIR})
target_link_libraries(test_features PRIVATE kissfft_lib)
target_compile_definitions(test_features PRIVATE PT_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
pt_add_test(wake ${PT_WAKE_DIR}/pt_wake_engine.cc ${PT_WAKE_DIR}/pt_features.c)
target_include_directories(test_wake PRIVATE ${PT_WAKE_DIR})
target_link_libraries(test_wake PRIVATE kissfft_lib tflm_lib)
target_compile_options(test_wake PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions>)
target_compile_definitions(test_wake PRIVATE
    PT_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden"
    PT_MODELS_DIR="${CMAKE_CURRENT_LIST_DIR}/../../models")
