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
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)
add_subdirectory(lib/http_client)
add_subdirectory(lib/imu)
add_subdirectory(lib/sd_card_driver)

include_directories(./lib/config)
include_directories(./lib/lcd)
include_directories(./lib/font)
include_directories(./lib/http_client)
include_directories(./lib/imu)
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

# air-writing trajectory estimation + file/payload formats (portable, also built on a PC by test/host/Makefile)
add_library(position_estimator src/position_estimator.cpp)
target_include_directories(position_estimator PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

# Deployment settings, e.g. cmake -DAW_SERVER_HOST=my.server -DAW_SERVER_PORT=5000 -DAW_API_TOKEN=secret
#   -DAW_WIFI_SSID=... -DAW_WIFI_PASSWORD=... -DAW_UPLOAD_RESAMPLE_POINTS=45 -DAW_LPF_CUTOFF_HZ=5
foreach(var AW_SERVER_HOST AW_API_TOKEN AW_WIFI_SSID AW_WIFI_PASSWORD)
    set(${var} "" CACHE STRING "air-writing setting (string)")
endforeach()
foreach(var AW_SERVER_PORT AW_UPLOAD_RESAMPLE_POINTS AW_LPF_CUTOFF_HZ)
    set(${var} "" CACHE STRING "air-writing setting (number)")
endforeach()


add_executable(main main.cpp) # main function to run.

target_include_directories(main
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/.
)


set_target_properties(main
    PROPERTIES
    COMPILE_FLAGS "-fno-rtti -fno-exceptions -fno-threadsafe-statics -nostdlib"
)

target_link_libraries(main 
    lcd 
    font 
    config
    http_client
    pico_stdlib 
    hardware_spi 
    hardware_pwm
    pico_multicore 
    hardware_adc
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mbedtls
    pico_mbedtls
    pico_lwip_sntp
    sd_card_wrapper
)

target_compile_options(main PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
)


set_property(TARGET main APPEND_STRING PROPERTY LINK_FLAGS 
    "-Wl,--print-memory-usage"
)
    

# enable usb output, disable uart output
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)


foreach(var AW_SERVER_HOST AW_API_TOKEN AW_WIFI_SSID AW_WIFI_PASSWORD)
    if(NOT "${${var}}" STREQUAL "")
        target_compile_definitions(main PRIVATE ${var}="${${var}}")
    endif()
endforeach()
foreach(var AW_SERVER_PORT AW_UPLOAD_RESAMPLE_POINTS AW_LPF_CUTOFF_HZ)
    if(NOT "${${var}}" STREQUAL "")
        target_compile_definitions(main PRIVATE ${var}=${${var}})
    endif()
endforeach()


# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)
