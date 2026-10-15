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

#include "DEV_Config.h"

#include "inference.h"


using namespace std;

int main(void) {

  System_Init();
  sleep_ms(5000);
  
  inference_test();
  return 0;
}