"""
Send a 28x28 MNIST-style image to the Pico over USB-serial/UART and print
back the prediction, inference time, and per-class confidence scores.

No LCD/touchscreen needed on the Pico side -- this talks to the
`main_uart_inference` build over whatever serial port the Pico enumerates as
(a USB CDC port on most systems, e.g. /dev/ttyACM0 on Linux, COM5 on
Windows, /dev/tty.usbmodemXXXX on macOS).

Usage:
    pip install pyserial numpy pillow
    python send_image.py /dev/ttyACM0 --npz mnist_cache.npz --index 42
    python send_image.py /dev/ttyACM0 --png my_digit.png

--npz / --index: pull a specific image out of an MNIST-format .npz file
           (expects an 'x_test' key, uint8, shape (N, 28, 28)).
--png:     load any 28x28 (or resizable-to-28x28) grayscale image, e.g. one
           of your own handwritten digits from Lab 2.2. Assumes a normal
           photo -- dark strokes on a light background -- and inverts to
           match MNIST's white-on-black convention, same as the Lab 2.2
           notebook's preprocessing.

For a zero-file smoke test first, flash `main_inference_test` instead (it
runs against 10 images already baked into the firmware) and compare its
output to this script once you're sending your own images.
"""

import argparse
import sys
import time

import numpy as np
import serial


def load_digit_from_npz(path, index):
    data = np.load(path)
    img = data["x_test"][index]  # uint8, shape (28, 28), values 0-255
    label = int(data["y_test"][index]) if "y_test" in data else None
    return img.astype(np.uint8), label


def load_digit_from_png(path):
    from PIL import Image

    img = Image.open(path).convert("L").resize((28, 28), Image.LANCZOS)
    arr = np.array(img).astype(np.float32)
    arr = 255.0 - arr  # invert: photo is dark-on-light, MNIST is light-on-dark
    return arr.astype(np.uint8), None


def send_and_predict(port, pixels, baudrate=115200, timeout=5):
    line = ",".join(str(int(p)) for p in pixels.flatten())

    with serial.Serial(port, baudrate=baudrate, timeout=timeout) as ser:
        # Drain any startup banner (the firmware prints "READY,<n>" once).
        time.sleep(0.2)
        ser.reset_input_buffer()

        ser.write((line + "\n").encode("ascii"))
        response = ser.readline().decode("ascii").strip()

    return response


def parse_response(response):
    parts = response.split(",")
    if parts[0] == "ERROR":
        return {"error": ",".join(parts[1:])}
    if parts[0] != "OK":
        return {"error": f"unrecognized response: {response}"}

    predicted = int(parts[1])
    inference_us = int(parts[2])
    scores = [float(x) for x in parts[3:]]
    return {"predicted": predicted, "inference_us": inference_us, "scores": scores}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--npz", help="Path to an MNIST-format .npz file")
    parser.add_argument("--index", type=int, default=0, help="Index into --npz's x_test array")
    parser.add_argument("--png", help="Path to a 28x28-ish grayscale PNG/JPG of your own handwriting")
    args = parser.parse_args()

    true_label = None
    if args.png:
        pixels, true_label = load_digit_from_png(args.png)
    elif args.npz:
        pixels, true_label = load_digit_from_npz(args.npz, args.index)
    else:
        print("Provide --npz or --png (see this script's docstring).", file=sys.stderr)
        sys.exit(1)

    print(f"Sending {pixels.size} pixels to {args.port}...")
    response = send_and_predict(args.port, pixels, baudrate=args.baudrate)
    result = parse_response(response)

    if "error" in result:
        print(f"Pico reported an error: {result['error']}")
        sys.exit(1)

    print(f"Predicted digit : {result['predicted']}")
    if true_label is not None:
        mark = "OK" if result["predicted"] == true_label else "WRONG"
        print(f"True label      : {true_label}  [{mark}]")
    print(f"Inference time  : {result['inference_us']} us")
    if result["scores"]:
        print("Per-class scores:")
        for digit, score in enumerate(result["scores"]):
            print(f"  {digit}: {score:.3f}")


if __name__ == "__main__":
    main()
