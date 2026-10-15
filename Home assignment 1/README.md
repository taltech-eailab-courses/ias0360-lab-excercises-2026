# Home Assignment 1: Image Preprocessing and Intermediate Comparisons

The project reached the stage of deploying the code to the Raspberry Pi Pico. This assignment focuses on the image-preprocessing pipeline and comparisons of its intermediate results; the embedded digit-recognition application is an extension of that work.

The main work in this assignment is the implementation and visualization of an image-preprocessing pipeline. It includes normalization, resizing, spatial averaging, downsampling, and 8-bit representation, together with comparisons of the intermediate image outputs. As an optional extension, the processed image was also used for handwritten-digit recognition in a Raspberry Pi Pico firmware with a compact CNN.

The C/C++ implementation is an optional embedded deployment of the image-processing pipeline. The main assignment evaluation focuses on preprocessing and intermediate image comparisons.

## Selected application domain

The selected application domain is image processing. The main deliverable is the preprocessing pipeline and its intermediate image comparisons. The optional extension is handwritten-digit recognition: the same 28 x 28 representation is passed to a compact fully-int8-quantized CNN and can be deployed as Raspberry Pi Pico firmware.

The implemented processing pipeline is:

1. Source image: load a handwritten digit sample.
2. Normalization: convert each value to [0, 1].
3. Spatial processing: resize to 28 x 28 and reduce larger buffers with 3 x 3 averaging.
4. Quantization: map normalized values to the input tensor's int8 scale and zero point.
5. Feature extraction: the CNN applies two convolution and max-pooling stages.

The CNN provides the feature extraction algorithm. It is suitable for this domain because convolutional filters learn local stroke, edge, and shape patterns without requiring hand-written features such as Sobel filters.

## Included model

`models/digit_model.tflite` and its embedded C representation in `models/mnist_model_data.cpp` are already included. The model is 20,152 bytes, uses int8 input/output tensors, and was trained on the local scikit-learn handwritten-digit dataset after resizing its images to 28 x 28. Its held-out validation accuracy was 96.39%.

To regenerate the model in the course Docker container:

```sh
python3 tools/train_quantized_model.py
```

The generator exports the `.tflite` model and the embedded C array with the correct names and alignment. The resulting model uses `CONV_2D`, `MAX_POOL_2D`, `RESHAPE`, `FULLY_CONNECTED`, and `SOFTMAX`.

To train a different architecture, pass the filter counts for the two convolution layers and the hidden dense-layer size. For example, train the small configuration and keep its outputs separate from the default model:

```sh
python3 tools/train_quantized_model.py \
	--conv-filters 4 8 \
	--dense-units 32 \
	--model-output models/digit_model_small.tflite \
	--cpp-output models/mnist_model_data_small.cpp
```

The defaults are `--conv-filters 8 16` and `--dense-units 32`. If output paths are omitted, the script overwrites the default model files.

## Preprocessing Comparisons

The final hardware photographs are separate from the preprocessing evidence. To generate intermediate image comparisons, run:

```sh
python3 tools/visualize_preprocessing.py --digit 7
```

The script creates these files in `report/images/preprocessing/`:

- `digit_7_pipeline.png` - original 8 x 8 sample, resized 28 x 28 image, 84 x 84 touch-like buffer, 3 x 3 reduction, and the resulting 8-bit visualization.
- `digits_reduced_comparison.png` - the ten digit classes after the 84 x 84 to 28 x 28 reduction.
- `filter_comparison.png` - visual comparison of 2 x 2, 3 x 3, and 5 x 5 average filters.
- `filter_metrics.csv` - MSE, RMSE, MAE, maximum absolute error, PSNR, correlation, and gradient RMSE for the three filters after reconstruction to 84 x 84.

These figures show the image-processing stages only; they are not photographs of the final Raspberry Pi demonstration. The 84 x 84 image is a reproducible visualization of the touch-buffer stage, and the last panel illustrates 8-bit representation. The exact model `int8` scale and zero point are read from the exported TFLite model by the firmware during inference.

The default experiment records MSE, RMSE, MAE, maximum absolute error, PSNR, signal correlation, and gradient RMSE. It produced PSNR values of 44.54 dB for the 2 x 2 filter, 41.89 dB for the 3 x 3 filter, and 20.41 dB for the 5 x 5 filter. Correlation was 0.9998 for the 2 x 2 and 3 x 3 filters and 0.9516 for the 5 x 5 filter. The 3 x 3 filter is therefore a useful compromise between smoothing and preservation of image structure.

## Optional Pico Deployment

The preprocessing comparisons do not require a Raspberry Pi Pico. The firmware deployment is optional and can be built inside the course Docker container, where CMake and the Pico SDK are installed:

```sh
cd "Home assignment 1"
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
cd ../..
./flash.sh "Home assignment 1/build/digit_recognition.uf2"
```

If the model does not fit, first keep the default tensor arena size. Then report `interpreter->arena_used_bytes()` and set `kTensorArenaSize` in `src/model_settings.h` to that value plus a small safety margin.

## Firmware Files and Final Sizes

The firmware is built as the `digit_recognition` target. Its main project files are:

- `main.cpp` - initializes the Pico, runs the touchscreen interface on one core, and performs inference on the other.
- `src/model.cpp`, `src/model.h`, and `src/model_settings.h` - configure and run the TensorFlow Lite Micro model.
- `models/mnist_model_data.cpp` and `models/mnist_model_data.h` - embed the quantized model bytes into the firmware. The original model is `models/digit_model.tflite`.
- `../lab_2_4/pico_sw/lib/lcd/`, `config/`, and `font/` - shared LCD, touch, board configuration, and font libraries included by CMake; these files are outside the Home assignment 1 folder.

Flash this file to the Raspberry Pi Pico:

```text
build/digit_recognition.uf2
```

From the workspace root, the flash command is `./flash.sh "Home assignment 1/build/digit_recognition.uf2"`. The compiled files currently in `build/` and `models/` have these sizes:

| File | Size | Purpose |
| --- | ---: | --- |
| `build/digit_recognition.uf2` | 347,648 bytes (339.50 KiB) | Firmware image to flash to the Pico |
| `models/digit_model.tflite` | 20,152 bytes (19.68 KiB) | Quantized model before embedding |
| `models/mnist_model_data.cpp` | 124,400 bytes (121.48 KiB) | C++ source representation of the model bytes |
| `build/digit_recognition.elf` | 7,040,344 bytes (6.71 MiB) | Build/debug executable; do not flash this file |

These are the sizes of the artifacts currently present in this workspace; rebuilding or retraining can change them.

## Optional LCD Interface

The Waveshare LCD touchscreen is an optional input interface. If the firmware is used, it reduces the drawing to a 28 x 28 grayscale image and passes it to the same CNN used by the project.

The expected display output has this structure:

```text
Prediction: <0-9>  Confidence: <0-100>%
```

Tap `CLEAR` after a prediction to erase the drawing and classify another digit.

## Report Summary

The report focuses on the image-processing algorithms used for handwritten digit recognition. It explains normalization, bilinear resizing, 3 x 3 block averaging, downsampling, 8-bit representation, and the comparison of intermediate outputs. It then describes convolution and max-pooling as learned feature-extraction stages. The Pico firmware and LCD interface are documented as optional deployment support, not as the main evaluation target.
