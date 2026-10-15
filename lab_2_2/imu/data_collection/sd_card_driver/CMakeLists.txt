add_library(sd_custom_driver INTERFACE)

pico_generate_pio_header(sd_custom_driver ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SDIO/rp2040_sdio.pio)

target_compile_definitions(sd_custom_driver INTERFACE
    PICO_MAX_SHARED_IRQ_HANDLERS=8u
)
# target_compile_options(sd_custom_driver INTERFACE -ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=)

target_sources(sd_custom_driver INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ffsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/ff15/source/ffunicode.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/dma_interrupts.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/sd_card.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SDIO/rp2040_sdio.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SDIO/sd_card_sdio.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SPI/my_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SPI/sd_card_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/sd_driver/SPI/sd_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/crash.c
    ${CMAKE_CURRENT_LIST_DIR}/src/crc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/f_util.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ff_stdio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/file_stream.c
    ${CMAKE_CURRENT_LIST_DIR}/src/glue.c
    ${CMAKE_CURRENT_LIST_DIR}/src/my_debug.c
    ${CMAKE_CURRENT_LIST_DIR}/src/my_rtc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_timeouts.c
    ${CMAKE_CURRENT_LIST_DIR}/src/util.c
)
target_include_directories(sd_custom_driver INTERFACE
    ff15/source
    sd_driver
    include
)
target_link_libraries(sd_custom_driver INTERFACE
    hardware_dma
    hardware_pio
    hardware_spi
    pico_aon_timer
    pico_stdlib
    cmsis_core
)
