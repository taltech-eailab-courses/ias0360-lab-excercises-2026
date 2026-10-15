
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
        uint8_t* input_data();
        int byte_size();
        const TfLiteTensor* output_tensor();
        
        const tflite::Model* model = nullptr;
        TfLiteTensor* input = nullptr;
        tflite::MicroInterpreter* interpreter = nullptr;
        tflite::ErrorReporter* error_reporter = nullptr;
    private:
};

#endif // TFLITE_INFERENCE_TEST_MODEL_H_