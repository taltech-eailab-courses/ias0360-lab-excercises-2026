/*****************************************************************************
* | File      	:	LCD_Touch.h
* | Author      :   Waveshare team
* | Function    :	LCD Touch Pad Driver and Draw
* | Info        :
*   Image scanning
*      Please use progressive scanning to generate images or fonts
*----------------
* |	This version:   V1.0
* | Date        :   2017-08-16
* | Info        :   Basic version
*
******************************************************************************/
#ifndef TFLITE_INFERENCE_TEST_LCD_TOUCH_H_
#define TFLITE_INFERENCE_TEST_LCD_TOUCH_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/float.h"
#include "pico/multicore.h"
#include "pico/sync.h"


#define TP_PRESS_DOWN           0x80
#define TP_PRESSED              0x40

#define IGNORE_INTERVAL_MS 200
#define DIGIT_INPUT_COUNT 4
#define BOX_SIZE 84
#define BOX_START_Y 120  // Starting Y position for the boxes
#define BORDER_THICKNESS 2
#define BOX_PADDING 4
#define POINT_SPACE 2
#define WINDOW_SIZE 3
#define SPACE_BETWEEN_BOXES 10

//Touch screen structure
typedef struct {
	POINT Xpoint0;
	POINT Ypoint0;
	POINT Xpoint;
	POINT Ypoint;
	uint8_t chStatus;
	uint8_t chType;
	int16_t iXoff;
	int16_t iYoff;
	float fXfac;
	float fYfac;
	//Select the coordinates of the XPT2046 touch \
	  screen relative to what scan direction
	LCD_SCAN_DIR TP_Scan_Dir;
}TP_DEV;

//Brush structure
typedef struct{
	POINT Xpoint;
	POINT Ypoint;
	COLOR Color;
	DOT_PIXEL DotPixel; 
}TP_DRAW;

typedef struct{
	uint8_t InputData[INPUT_IMAGE_SIZE * INPUT_IMAGE_SIZE]; // 784 uint8_t array
	int8_t PredictedDigit;
} USER_INPUT;

typedef struct{
	// semaphore_t Semaphore;
	bool IsProcessing;
	USER_INPUT UserInputs[DIGIT_INPUT_COUNT]; // 4 inputs
} INFERENCE;

typedef struct{
	int start_x;
	int start_y;
	int end_x;
	int end_y;
	uint8_t content[BOX_SIZE][BOX_SIZE];
} BOX_REFERENCE;

void TP_GetAdFac(void);
void TP_Adjust(void);
void TP_Dialog(void);
void TP_DrawBoard(void);
void TP_Init( LCD_SCAN_DIR Lcd_ScanDir );

void init_gui(void);
void reset_inference(INFERENCE* _inference);

int find_box_by_point(void);
void clear_drawing();
void draw_inference_result();



#ifdef __cplusplus
}
#endif

#endif // TFLITE_INFERENCE_TEST_LCD_TOUCH_H_
