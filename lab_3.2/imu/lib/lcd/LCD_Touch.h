#ifndef __LCD_TOUCH_H_
#define __LCD_TOUCH_H_

#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/float.h"

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"

#define TP_PRESS_DOWN           0x80
#define TP_PRESSED              0x40


// ---- Capture area (the black box) ------------------------------------------
// 480×320
#define LCD_W 480
#define LCD_H 320

#define POINT_SPACE 2
#define IGNORE_INTERVAL_MS 200

#ifdef __cplusplus
extern "C" {
#endif

extern LCD_DIS sLCD_DIS;
extern uint8_t id;

enum State {
    DEFAULT,    
	PROCESSING,
	SUCCESS
};

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

void TP_GetAdFac(void);
void TP_Adjust(void);
void TP_Dialog(void);
void TP_Save(void);
void TP_DrawBoard(void);
uint8_t TP_Scan(uint8_t tp);
void TP_Init( LCD_SCAN_DIR Lcd_ScanDir, TP_DEV* tp_dev, TP_DRAW* tp_draw);
void TP_display_input(int h, int w, const uint8_t* src);
void TP_DrawHeader(void);

#ifdef __cplusplus
}
#endif

#endif
