#ifndef TFLITE_INFERENCE_TEST_MODEL_H_
#define TFLITE_INFERENCE_TEST_MODEL_H_

#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

class Model {
    public:
        Model();
        virtual ~Model();

        int setup();
        int predict();

        // Normalizes a raw 0-255 grayscale image (image_row_size * image_col_size
        // bytes, row-major) to [0,1] the same way training data was normalized,
        // then quantizes it into the model's int8 input tensor using that
        // tensor's own (scale, zero_point) -- read from the model itself, not
        // hard-coded, so this keeps working if you requantize with different
        // parameters later. Returns false if the model isn't set up yet.
        bool set_input_image(const uint8_t* pixels);

        // Dequantizes the output tensor's `count` scores into `out_scores`
        // (caller-allocated, must hold at least `count` floats), using the
        // output tensor's own (scale, zero_point) -- same "read it from the
        // model, don't hard-code it" approach as set_input_image(). Returns
        // false if the model isn't set up yet. Useful for reporting a full
        // per-digit confidence breakdown, not just the winning class.
        bool get_output_scores(float* out_scores, int count);

        uint8_t* input_data();
        int byte_size();

        const tflite::Model* model = nullptr;
        TfLiteTensor* input = nullptr;
        tflite::MicroInterpreter* interpreter = nullptr;
        tflite::ErrorReporter* error_reporter = nullptr;
    private:
        float input_scale_ = 1.0f;
        int input_zero_point_ = 0;
};

#endif // TFLITE_INFERENCE_TEST_MODEL_H_
