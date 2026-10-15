# Magic Wand Application

This application is based on TensorFlow Lite's [Magic Wand example](https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/magic_wand), adapted to run on the hardware used in this course. It uses an IMU (accelerometer) sensor to capture hand-gesture motion data and applies a machine learning model to classify the gesture that was performed.

## Overview

The end goal of this application is to:

1. **Collect** raw accelerometer (IMU) data while a gesture is performed with the sensor in hand.
2. **Train** a gesture-classification machine learning model offline using the collected data.
3. **Deploy and run** the trained model on-device to classify gestures in real time from live IMU readings.

## Gesture Classes

The dataset and the classifier distinguish between four classes:

| Class | Description |
|-------|-------------|
| **Negative** | Stationary / no gesture (idle motion, used as the "no gesture" class) |
| **Ring** | A circular, "O"-shaped gesture |
| **Slope** | An "L"-shaped gesture |
| **Wing** | A "W"-shaped gesture |

## IMU Orientation

The example dataset was recorded with the Eval-board screen facing down, so that the IMU's axes point as follows:

| Axis | Direction |
|------|-----------|
| **x** | Rightward |
| **y** | Backward |
| **z** | Upward |

See [assets/Eval-board-imu-orientation.png](assets/Eval-board-imu-orientation.png) for a visual reference of this orientation.

## Project Structure

- [`data_collection/`](data_collection) - Firmware for the Raspberry Pi Pico that reads accelerometer samples from the ICM20948 IMU and logs them to an SD card. See [`data_collection/README.md`](data_collection/README.md) for build, configuration, and data-format details.
- [`dataset/`](dataset) - Example IMU recordings collected with `data_collection`, organized into one subfolder per gesture class ([`negative/`](dataset/negative), [`ring/`](dataset/ring), [`slope/`](dataset/slope), [`wing/`](dataset/wing)). Each `.txt` file is a CSV recording of one gesture sample with an `ax,ay,az` header followed by raw accelerometer readings.
- [`assets/`](assets) - Supporting images, including the IMU orientation diagram referenced above.

## Workflow

1. Flash the `data_collection` firmware onto the Pico and record IMU samples for each gesture class, saving them to the SD card (see [`data_collection/README.md`](data_collection/README.md) for configuration options such as recording duration, sampling rate, and number of sessions).
2. Copy the recorded `.txt` files into the appropriate class folder under `dataset/`, matching the layout used for the provided example dataset.
3. Use the collected dataset to train a gesture-classification model (e.g. following the approach of the TensorFlow Lite Magic Wand example).
4. Convert the trained model to TensorFlow Lite format and deploy it to the device to classify gestures from live IMU data.
