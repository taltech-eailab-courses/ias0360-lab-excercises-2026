#ifndef __LCD_APP_H_
#define __LCD_APP_H_

#include <stdint.h>
#include "pico/util/queue.h"

#include "LCD_utils.hpp"
#include "DEV_Config.h"
#include "LCD_Touch.h"


#define BUTTON_W 100
#define BUTTON_H 40
#define SHORT_BUTTON_W 80
#define LONG_BUTTON_W 240
#define RESULT_TEXT_W 360
#define RESULT_TEXT_H 50

#define MARGIN 10
#define MARGIN_2 2*MARGIN
#define BOX_X0 60
#define BOX_Y0 80
#define BOX_X1 420   // right  edge (exclusive in our math)
#define BOX_Y1 200   // bottom edge (exclusive in our math)

#define DATA_SIZE_W (BOX_X1 - BOX_X0) // 360
#define DATA_SIZE_H (BOX_Y1 - BOX_Y0) // 120

#define BOX_W (MARGIN_2 + DATA_SIZE_W)   // 360 + 20
#define BOX_H (MARGIN_2 + DATA_SIZE_H)   // 120 + 20

#define MIDDLE_BUTTON_X (LCD_W / 2) - MARGIN
#define MIDDLE_BUTTON_Y (LCD_H / 2) - (BUTTON_H / 2)
#define BOTTOM_BUTTON_X MIDDLE_BUTTON_X
#define BOTTOM_BUTTON_Y LCD_H - BUTTON_H - MARGIN

// Number of characters selection screen
#define START_IMU_X0 ((LCD_W / 2) - (LONG_BUTTON_W / 2))
#define START_IMU_Y0 BOTTOM_BUTTON_Y
#define NUM_DIGIT_X0 (LCD_W / 2)-MARGIN
#define NUM_DIGIT_Y0 (LCD_H/2)-MARGIN
#define NUM_SELECT_NEXT_X0 ((LCD_W / 2) + (4*MARGIN)) 
#define NUM_SELECT_NEXT_Y0 ((LCD_H / 2) - (BUTTON_H / 2))
#define NUM_SELECT_BACK_X0 ((LCD_W / 2) - SHORT_BUTTON_W - (4*MARGIN)) 
#define NUM_SELECT_BACK_Y0 NUM_SELECT_NEXT_Y0

// OCR result screen
#define OCR_RESULT_X0 ((LCD_W / 2) - (3*MARGIN))
#define OCR_RESULT_Y0 ((LCD_H / 2) - (2*MARGIN))
#define BACK_TO_DEFAULT_SCREEN_X0 ((LCD_W / 2) - (BUTTON_W / 2))
#define BACK_TO_DEFAULT_SCREEN_Y0 BOTTOM_BUTTON_Y

// Send data to the cloud or rerecording view
#define SEND_X0 ((LCD_W / 2) + (2*MARGIN))
#define SEND_Y0 MIDDLE_BUTTON_Y
#define RERECORD_X0 ((LCD_W / 2) - (4*MARGIN) - BUTTON_W)
#define RERECORD_Y0 MIDDLE_BUTTON_Y

// Menu button and its view
#define MENU_X0 10
#define MENU_Y0 10
#define MENU_W 30	
#define MENU_H 30
#define MENU_BUTTON_W (BUTTON_W + 30)
#define BACK_X0 (LCD_W / 2 - (MENU_BUTTON_W / 2))
#define BACK_Y0 (LCD_H / 2 - (BUTTON_H / 2) - 30)
#define LOGOUT_X0 BACK_X0
#define LOGOUT_Y0 (LCD_H / 2 - (BUTTON_H / 2) + 30)



#define RESULT_W 180
#define RESULT_H 60
#define DATA_SIZE (RESULT_W*RESULT_H)

#define MAX_CHAR_COUNTS 5

// ---------------------------------------------------------------------------------------------------
// Messages between the UI (core 0) and the worker (core 1). Two Pico SDK software queues; the elements are
// plain structs that are copied into the queue (they own all their data, no pointers to temporary buffers).
//   command_queue: UI -> worker, capacity COMMAND_QUEUE_DEPTH. The UI uses queue_try_add(); a full queue is shown
//                  to the user, never waited on. The idle worker uses queue_remove_blocking().
//   event_queue:   worker -> UI, capacity EVENT_QUEUE_DEPTH. The UI drains it with queue_try_remove().
//                  Progress events (COUNTDOWN, RECORDING, UPLOAD_PROGRESS) are coalesced: if the queue is full only
//                  the newest one is kept and sent as soon as there is room. Completion and error events are sent
//                  with queue_add_blocking() and are never dropped.
// ---------------------------------------------------------------------------------------------------
#define COMMAND_QUEUE_DEPTH 4
#define EVENT_QUEUE_DEPTH 32
#define EVENT_TEXT_MAX 48
#define NO_CHAR 0xFF

typedef enum { CMD_START_CAPTURE = 1, CMD_SEND_WORD = 2 } CommandType;

typedef struct {
	uint8_t type;        // CommandType
	uint8_t char_count;  // 1..5
	uint16_t reserved;
	uint32_t session_id; // START_CAPTURE: new id chosen by the UI; SEND_WORD: the saved session
} AppCommand;

typedef enum {
	EVT_COUNTDOWN = 1,        // value = seconds left
	EVT_RECORDING,            // value = elapsed ms of the 3 s window
	EVT_CHARACTER_SAVED,      // flags = quality flags (bit0: drift corrected)
	EVT_ALL_SAVED,
	EVT_UPLOAD_PROGRESS,      // char_index = being sent, value = characters acknowledged
	EVT_WAITING_FOR_OCR,
	EVT_OCR_RESULT,           // text = recognised word
	EVT_ERROR                 // stage, error, retryable, detail, text
} EventType;

typedef enum { STAGE_CAPTURE = 1, STAGE_SAVE, STAGE_UPLOAD, STAGE_OCR } ErrorStage;

typedef enum {
	AERR_NONE = 0, AERR_BAD_COMMAND, AERR_IMU_NOT_FOUND, AERR_IMU_READ, AERR_ESTIMATOR, AERR_STORAGE_MOUNT,
	AERR_STORAGE_WRITE, AERR_STORAGE_READ, AERR_WIFI_JOIN, AERR_DNS, AERR_CONNECT, AERR_TIMEOUT, AERR_CONNECTION_RESET,
	AERR_SERVER_UNAVAILABLE, AERR_SERVER_REJECTED, AERR_BAD_RESPONSE
} ErrorCode;

typedef struct {
	uint8_t type;        // EventType
	uint8_t char_index;  // 0-based, NO_CHAR if not applicable
	uint8_t char_count;
	uint8_t flags;
	uint32_t session_id;
	uint16_t value;
	uint8_t stage;       // ErrorStage
	uint8_t error;       // ErrorCode
	uint8_t retryable;
	int32_t detail;
	char text[EVENT_TEXT_MAX];
} AppEvent;

extern queue_t command_queue;
extern queue_t event_queue;
void app_queues_init(void);
void app_post_progress(const AppEvent* e);  // worker side, never blocks, coalesces
void app_post_critical(const AppEvent* e);  // worker side, blocks until queued


typedef struct {
	uint8_t number_of_letters;
	State state;
	uint8_t data[DATA_SIZE];
	char* response;
} OCR_Inference;

typedef enum {
    NUM_SELECT,
    CONFIRM,        // Start / Back
    SEND_DATA,      // "All characters are saved!" + Send to server
    OCR_RESULT,     // upload progress, waiting for OCR, predicted text
    MENU_OPEN,
    RECORDING,      // countdown + recording of every character
    ERROR_VIEW
} ScreenState;


void APP_Init(LCD_SCAN_DIR lcd_scan_dir, OCR_Inference* _inference);
void APP_ShowCapturePrompt(uint8_t index, uint8_t count, const char* line);   // core 0, only while core 1 is idle
void APP_Poll(void);   // drains the event queue and redraws; call in the UI loop together with TP_DrawBoard()


#endif // __LCD_APP_H_