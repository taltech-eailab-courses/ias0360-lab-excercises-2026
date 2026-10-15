aux_source_directory(. DIR_SRC_SRCS)

include_directories(../lib/pico-tflmicro)

add_library(model ${DIR_SRC_SRCS})
target_link_libraries(
  model 
  PUBLIC 
  pico_stdlib 
  pico-tflmicro
)
