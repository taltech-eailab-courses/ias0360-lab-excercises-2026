#include <cmath> 
#include<iostream>
#include <cstdlib> 
#include <iostream>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

extern "C" {
	#include "DEV_Config.h"
}


#include "model.h"
#include "model_settings.h"
#include "gesture_model_data.h"

using namespace std; 
Model ml_model;

int main() {

  stdio_init_all();
  
  sleep_ms(3000);
  printf("Starting the arena size test...\n");
  
  // initialize ML model
  if (!ml_model.setup()) {
    printf("Failed to initialize ML model!\n");
    return -1;
  }
  printf("Model initialized\n");
  
  uint8_t* test_image_input = ml_model.input_data();
  if (test_image_input == nullptr) {
    printf("Cannot set input\n");
    return -1;
  }
  
  int byte_size = ml_model.byte_size();
  if (!byte_size) {
    printf("Byte size not found\n");
    return -1;
  }


  while(1) {
    printf("The tensor arena size: %d\n", ml_model.interpreter->arena_used_bytes());
    sleep_ms(1000);
  }
  return 0;
}