# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w)
# set(PICO_BOARD pico2_w CACHE STRING "Board type")

set(PROGRAM_NAME main)

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(${PROGRAM_NAME} C CXX ASM)
set(PICO_CYW43_ARCH lwip_sys_freertos CACHE STRING "" FORCE)


pico_sdk_init()


add_subdirectory(lib/config)
add_subdirectory(lib/model)
add_subdirectory(lib/imu)
add_subdirectory(lib/pico-tflmicro)

include_directories(./lib/config)
include_directories(./lib/model)
include_directories(./lib/imu)
include_directories(./lib/pico-tflmicro)


add_library(data_preprocessor
    src/data_preprocessor.cpp
    src/data_preprocessor.hpp
)

target_include_directories(data_preprocessor PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(data_preprocessor
    pico_stdlib
)

add_executable(main main.cpp) # main function to run.
add_executable(tensor_arena_test arena_size_test.cpp) # to check arena size for a given model.
add_executable(local_inference_test local_inference_test.cpp) # to test hand written digit recognition with static test data.
add_executable(local_inference_raw_test local_inference_raw_test.cpp) # to test hand written digit recognition with static test data.

target_include_directories(main
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(tensor_arena_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(local_inference_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)
target_include_directories(local_inference_raw_test
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)

set_target_properties(main
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

set_target_properties(tensor_arena_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(local_inference_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)
set_target_properties(local_inference_raw_test
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

target_link_libraries(main 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    hardware_pio
    hardware_i2c
    pico_util
    icm20948
    data_preprocessor
    pico-tflmicro
    data_preprocessor
    pico_cyw43_arch_none
)

target_link_libraries(
    tensor_arena_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    pico-tflmicro
)
target_link_libraries(
    local_inference_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    data_preprocessor
    pico_cyw43_arch_none
)
target_link_libraries(
    local_inference_raw_test 
    config
    model
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    pico_multicore 
    hardware_adc
    data_preprocessor
    pico_cyw43_arch_none
)


target_compile_options(main PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
)

set_property(TARGET main APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET tensor_arena_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET local_inference_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
set_property(TARGET local_inference_raw_test APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)

# enable usb output, disable uart output
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)
pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

pico_enable_stdio_usb(tensor_arena_test 1)
pico_enable_stdio_uart(tensor_arena_test 1)

pico_enable_stdio_usb(local_inference_test 1)
pico_enable_stdio_uart(local_inference_test 1)

pico_enable_stdio_usb(local_inference_raw_test 1)
pico_enable_stdio_uart(local_inference_raw_test 1)


# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)

pico_add_extra_outputs(tensor_arena_test)

pico_add_extra_outputs(local_inference_test)
pico_add_extra_outputs(local_inference_raw_test)
