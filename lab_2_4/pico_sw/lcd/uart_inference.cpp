// UART / USB-serial inference harness.
//
// Lets a student send a 784-pixel MNIST image as plain text over serial
// (USB CDC or a real UART wire -- both work identically here, since
// pico_enable_stdio_usb and pico_enable_stdio_uart are both on) and get
// back the predicted digit, inference time, and per-class confidence
// scores. No LCD/touchscreen required -- that interactive path in main.cpp
// is still there if you have the hardware, but this is the path everyone
// can use.
//
// Protocol (one line in, one line out):
//
//   Host -> Pico:  784 pixel values (0-255), comma- or whitespace-separated,
//                  one line, terminated by '\n'. Order is row-major, same
//                  as the MNIST arrays used throughout this lab.
//
//   Pico -> Host:  OK,<predicted_digit>,<inference_time_us>,<score_0>,...,<score_9>
//              or  ERROR,<message>
//
// Example line in  (truncated for readability -- yours will have 784 numbers):
//   0,0,0,...,34,128,255,...,0
// Example line out:
//   OK,7,842,0.001,0.000,0.003,0.002,0.004,0.001,0.000,0.971,0.010,0.008
//
// See host_scripts/send_image.py for a ready-to-use Python sender.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pico/stdlib.h"
#include "DEV_Config.h"

#include "model.h"
#include "model_settings.h"

#define LINE_BUFFER_SIZE 4096

Model ml_model;

// Reads one line from stdio (USB CDC or UART, whichever the host is using)
// into `buffer`, up to `max_len - 1` characters. Blocks until it sees '\n'
// or the buffer fills. Returns the number of characters read (not counting
// the terminator).
static int read_line(char* buffer, int max_len) {
  int len = 0;
  while (len < max_len - 1) {
    int c = getchar();
    if (c == '\n' || c == '\r') {
      if (len == 0) {
        // Ignore leading blank lines (e.g. a stray \r\n pair).
        continue;
      }
      break;
    }
    if (c == PICO_ERROR_TIMEOUT) {
      continue;
    }
    buffer[len++] = static_cast<char>(c);
  }
  buffer[len] = '\0';
  return len;
}

// Parses up to `max_count` comma/whitespace-separated integers out of `line`
// into `out_values` (each clamped to 0-255). Returns how many it found.
static int parse_pixel_line(const char* line, uint8_t* out_values, int max_count) {
  int count = 0;
  const char* p = line;
  while (*p != '\0' && count < max_count) {
    while (*p == ',' || *p == ' ' || *p == '\t') p++;
    if (*p == '\0') break;

    char* end = nullptr;
    long value = strtol(p, &end, 10);
    if (end == p) break;  // no digits found, stop

    if (value < 0) value = 0;
    if (value > 255) value = 255;
    out_values[count++] = static_cast<uint8_t>(value);

    p = end;
  }
  return count;
}

int main(void) {
  System_Init();
  sleep_ms(2000);

  if (!ml_model.setup()) {
    printf("ERROR,failed to initialize model\n");
    while (true) { tight_loop_contents(); }
  }
  printf("READY,%d\n", kMaxImageSize);  // tells the host how many pixels to send

  static char line_buffer[LINE_BUFFER_SIZE];
  static uint8_t pixels[kMaxImageSize];

  while (true) {
    int line_len = read_line(line_buffer, LINE_BUFFER_SIZE);
    if (line_len == 0) {
      continue;
    }

    int n_parsed = parse_pixel_line(line_buffer, pixels, kMaxImageSize);
    if (n_parsed != kMaxImageSize) {
      printf("ERROR,expected %d pixel values, got %d\n", kMaxImageSize, n_parsed);
      continue;
    }

    if (!ml_model.set_input_image(pixels)) {
      printf("ERROR,failed to set input image\n");
      continue;
    }

    absolute_time_t start = get_absolute_time();
    int result = ml_model.predict();
    int64_t inference_us = absolute_time_diff_us(start, get_absolute_time());

    if (result == -1) {
      printf("ERROR,inference failed\n");
      continue;
    }

    float scores[kCategoryCount];
    bool have_scores = ml_model.get_output_scores(scores, kCategoryCount);

    printf("OK,%d,%lld", result, (long long)inference_us);
    if (have_scores) {
      for (int i = 0; i < kCategoryCount; i++) {
        printf(",%.3f", scores[i]);
      }
    }
    printf("\n");
  }

  return 0;
}
