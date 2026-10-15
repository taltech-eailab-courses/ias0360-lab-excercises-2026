#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

#include "model.h"
#include "model_settings.h"
// TODO: 4. Import your model data


Model::Model() :
    model(nullptr),
    interpreter(nullptr),
    input(nullptr),
    error_reporter(nullptr)
{
}

Model::~Model()
{
    if (interpreter != NULL) {
        delete interpreter;
        interpreter = NULL;
    }
    if (model != NULL) {
        delete model;
        model = NULL;
    }
    if (input != NULL) {
        delete input;
        input = NULL;
    }
    if (error_reporter != NULL) {
        delete error_reporter;
        error_reporter = NULL;
    }
}

int Model::setup() 
{
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model = tflite::GetModel(gesture_model_data);
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

  return 1;
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

const TfLiteTensor* Model::output_tensor() {
  if (interpreter == nullptr) {
    return nullptr;
  }
  return interpreter->output(0);
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