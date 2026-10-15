aux_source_directory(. DIR_MODELS_SRCS)


include_directories(../src)

add_library(models ${DIR_MODELS_SRCS})

target_include_directories(models PUBLIC src)