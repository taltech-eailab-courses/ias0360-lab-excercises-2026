#include "mnist_model_data.h"

alignas(8) const unsigned char mnist_model_data[] = {
  // TODO 1: Copy your generated C array here.
  //
  // In the pruning_quantization notebook (Part C), the "xxd -i" cell writes
  // a file called mnist_model_data.cpp with the array already renamed and
  // aligned correctly -- just copy that whole file over this one, or copy
  // its array contents into this block.
};

// TODO 2: Set this to the actual byte length of your .tflite file (the
// notebook's Part C prints this value -- it should match len(tflite_bytes)
// exactly, or the interpreter will read garbage past the end of the array).
const int mnist_model_data_len = -1;
