aux_source_directory(. DIR_LCD_SRCS)

include_directories(../config)
include_directories(../font)

add_library(lcd ${DIR_LCD_SRCS})
target_include_directories(lcd PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../src)   # position_estimator.hpp (quality flags)
target_link_libraries(lcd PUBLIC config font pico_stdlib pico_util pico_sync pico_rand hardware_spi)
