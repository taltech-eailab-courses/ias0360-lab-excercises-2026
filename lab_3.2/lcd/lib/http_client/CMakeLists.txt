aux_source_directory(. DIR_HTTP_CLIENT_SRCS)

add_library(http_client ${DIR_HTTP_CLIENT_SRCS})

target_include_directories(http_client PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_SOURCE_DIR})

target_link_libraries(http_client PUBLIC
	pico_stdlib
	pico_cyw43_arch_lwip_threadsafe_background
	pico_lwip_http
	pico_lwip_mbedtls
)

# Ensure mbedtls headers are available while building this library
target_link_libraries(http_client PRIVATE pico_mbedtls)

# Use our mbedtls config
target_compile_definitions(http_client PUBLIC PICO_MBEDTLS_CONFIG_FILE="lib/http_client/mbedtls_config.h")
