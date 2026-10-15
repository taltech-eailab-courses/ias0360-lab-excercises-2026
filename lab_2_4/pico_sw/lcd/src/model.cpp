#include <algorithm>
#include <cmath>

#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#include "model.h"
#include "model_settings.h"
// TODO 4: Import your model data header (see models/mnist_model_data.cpp --
// its declarations live in mnist_model_data.h).


Model::Model() :
    model(nullptr),
    interpreter(nullptr),
    input(nullptr),
    error_reporter(nullptr)
{
}

Model::~Model()
{
    // `interpreter`, `model`, and `input` all point at static storage set up
    // in setup() (see the `static` locals in setup()) -- they are not heap
    // allocated here, so there is nothing for this destructor to free.
}

int Model::setup()
{
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model = tflite::GetModel(/* TODO 5: Load your model -- pass the array from
                              mnist_model_data.cpp. */);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return 0;
  }

  // TODO 6: Change the op resolver size (the number in <...>) to match the
  // number of distinct ops you register below in TODO 7.
  //
  // Hint: the 2.2/2.3/2.4 CNN is
  //   Conv2D -> MaxPool2D -> Conv2D -> MaxPool2D -> Reshape (Flatten) ->
  //   FullyConnected -> FullyConnected
  // -- list out the *distinct* op types you see there (Reshape is what
  // TFLite converts Flatten into; the final Dense's softmax activation
  // shows up as its own SOFTMAX op). If you changed the architecture in
  // Lab 2.2, adjust this list to match.
  static tflite::MicroMutableOpResolver</* TODO 6 */> micro_op_resolver;
  // TODO 7: Register each op with the resolver, e.g.
  //   micro_op_resolver.AddConv2D();
  // If AllocateTensors() below fails with a "Didn't find op" error, that
  // tells you exactly which op is still missing here.


  static uint8_t tensor_arena[arena_size];
  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, arena_size);
  interpreter = &static_interpreter;

  // TODO 8: Allocate tensors (interpreter->AllocateTensors()), check the
  // returned TfLiteStatus, and report+return 0 on failure like the check
  // above does for the schema version.


  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

  // Cache the input tensor's quantization parameters (baked into the
  // .tflite file by the converter) so set_input_image() below doesn't need
  // them hard-coded.
  input_scale_ = input->params.scale;
  input_zero_point_ = input->params.zero_point;

  return 1;
}

bool Model::set_input_image(const uint8_t* pixels) {
  if (input == nullptr) {
    return false;
  }

  // TODO 11: For each of the kMaxImageSize pixels:
  //   1. Normalize the raw 0-255 pixel to [0, 1] (matching how training
  //      data was normalized in the Lab 2.2 notebook -- pixel / 255.0).
  //   2. Quantize it into the tensor's int8 range using
  //      round(normalized / input_scale_) + input_zero_point_.
  //   3. Clamp to [-128, 127] and store into input->data.int8[i].
  //
  // Why this step exists: earlier versions of this project just memcpy'd
  // raw 0-255 bytes straight into the input tensor. That's only correct if
  // the model was quantized against raw 0-255 representative data -- ours
  // was quantized against data normalized to [0,1] (matching the Lab 2.2
  // training pipeline), so the on-device input has to go through the same
  // transform or predictions will be wrong despite the model "running".

  return true;
}

bool Model::get_output_scores(float* out_scores, int count) {
  if (interpreter == nullptr) {
    return false;
  }
  TfLiteTensor* output = interpreter->output(0);
  float scale = output->params.scale;
  int zero_point = output->params.zero_point;

  for (int i = 0; i < count; i++) {
    // Dequantize: real_value = (quantized - zero_point) * scale
    out_scores[i] = (static_cast<int>(output->data.int8[i]) - zero_point) * scale;
  }
  return true;
}

uint8_t* Model::input_data() {
  if (input == nullptr) {
    return nullptr;
  }
  return input->data.uint8;
}

int Model::byte_size() {
  if (input == nullptr) {
    return 0;
  }
  return input->bytes;
}

int Model::predict()
{
  printf("Invocation started\n");
  // TODO 9: Run interpreter->Invoke(). If it doesn't return kTfLiteOk,
  // report an error and return -1.

  printf("Invocation finished\n");

  TfLiteTensor* output = interpreter->output(0);

  int result = -1;
  // TODO 10: Return the index of the output neuron with the highest score.
  //
  // Output is int8 (full-integer quantized model). Quantization is a
  // monotonic transform, so argmax on the raw int8 scores gives the same
  // predicted class as argmax on the dequantized probabilities -- no need
  // to dequantize just to find the winner. Loop over
  // output->data.int8[0..kCategoryCount-1] and track the index of the
  // largest value.

  return result;
}
