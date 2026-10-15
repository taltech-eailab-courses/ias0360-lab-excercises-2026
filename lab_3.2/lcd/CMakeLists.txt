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

set(PROGRAM_NAME main)

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(${PROGRAM_NAME} C CXX ASM)
set(PICO_CYW43_ARCH lwip_sys_freertos CACHE STRING "" FORCE)


pico_sdk_init()


add_subdirectory(lib/http_client)
add_subdirectory(lib/sd_card_driver)

include_directories(./lib/http_client)
include_directories(./lib/sd_card_driver)

add_library(sd_card_wrapper
    src/sd_card_wrapper.cpp
    config/hw_config.c
)

target_include_directories(sd_card_wrapper PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/lib/sd_card_driver
)

target_link_libraries(sd_card_wrapper
    pico_stdlib
    sd_custom_driver
)

add_executable(${PROGRAM_NAME} main.cpp) # main function to run.

target_include_directories(${PROGRAM_NAME}
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)
set_target_properties(${PROGRAM_NAME}
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

target_link_libraries(${PROGRAM_NAME}
    sd_card_wrapper
    http_client
    pico_stdlib
    hardware_spi 
    hardware_pwm 
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
    pico_multicore
)

add_executable(test_multicore_fifo
    test.cpp
)

target_link_libraries(test_multicore_fifo 
    pico_stdlib
    pico_multicore    
    hardware_spi
    hardware_gpio
    FatFs_SPI
    sd_card_driver
)

                        
# target_compile_options(main PRIVATE
#     $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
# )



set_property(TARGET ${PROGRAM_NAME} APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
# enable usb output, disable uart output
pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_enable_stdio_uart(${PROGRAM_NAME} 0)


pico_add_extra_outputs(${PROGRAM_NAME})

pico_enable_stdio_usb(test_multicore_fifo 1)
pico_enable_stdio_uart(test_multicore_fifo 0)

pico_add_extra_outputs(test_multicore_fifo)
