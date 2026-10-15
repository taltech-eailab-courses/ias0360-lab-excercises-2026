#include <string.h>
#include <stdio.h>
#include <vector>
#include <cstdint>
#include "pico/multicore.h"

#include "LCD_app.h"

static OCR_Inference* inference;

static Button* menu_button;
static Button* back_button;
static Button* logout_button;

static Button* back_to_default_screen_button;
static Button* send_button;
static Button* rerecording_button;
static Button* num_select_start_button; // Start IMU reading
static Button* num_select_next_button; // Go to the next number selection
static Button* num_select_back_button; // Go back to the previous number selection

static uint8_t selected_number = 1;
static bool is_menu_open = false;
static ScreenState screen_state = NUM_SELECT;
static char ocr_text[EVENT_TEXT_MAX] = "";   // recognised word, empty while the request is in progress
static char status_message[48] = "";   // shown on the selection screen, e.g. after a failed recording

TP_DEV sTP_DEV;
TP_DRAW sTP_Draw;

queue_t command_queue;
queue_t event_queue;
static AppEvent pending_progress;
static bool has_pending_progress = false;
static uint32_t next_session_id = 1;

void app_queues_init(void)
{
    queue_init(&command_queue, sizeof(AppCommand), COMMAND_QUEUE_DEPTH);
    queue_init(&event_queue, sizeof(AppEvent), EVENT_QUEUE_DEPTH);
}

void app_post_progress(const AppEvent* e)
{
    // Keep only the newest progress event if the queue is full.
    if (has_pending_progress && queue_try_add(&event_queue, &pending_progress)) has_pending_progress = false;
    if (!queue_try_add(&event_queue, e)) {
        pending_progress = *e;
        has_pending_progress = true;
    }
}

void app_post_critical(const AppEvent* e)
{
    queue_add_blocking(&event_queue, e);
}

// UI side: never blocks. Returns false if the queue is full.
static bool send_command(CommandType type, uint8_t char_count, uint32_t session_id)
{
    AppCommand c = {};
    c.type = type;
    c.char_count = char_count;
    c.session_id = session_id;
    return queue_try_add(&command_queue, &c);
}


static bool prevent_interaction(void) 
{
    return (inference != NULL && (inference->state == PROCESSING || inference->state == SUCCESS));
}


void draw_ocr_prediction(void)
{

    // if (inference != NULL && inference->response != NULL) 
    // {
    //     GUI_DrawRectangle(OCR_RESULT_X0, OCR_RESULT_Y0,
    //                     OCR_RESULT_X0 + BOX_W, OCR_RESULT_Y0 + 36,
    //                     GREEN, DRAW_EMPTY, DOT_PIXEL_2X2);
        
    //     size_t response_length = strlen(inference->response);
    //     size_t prefix_length = strlen("Predicted: ");
    //     char* dynamic_display_str = (char*)malloc(response_length + prefix_length + 1);
    //     if (dynamic_display_str != NULL) {
    //         snprintf(dynamic_display_str, response_length + prefix_length + 1, "Predicted: %s", inference->response);
    //         // GUI_DisString_EN(text_field->get_x() + 20, result_y0 + 8,
    //         //                     dynamic_display_str, &Font20, WHITE, BLACK);
    //         free(dynamic_display_str);
    //     }
    //     free(inference->response);
    //     inference->response = NULL;
    // }
}

void draw_initial_screen(void) {
    LCD_Clear(LCD_BACKGROUND);
    GUI_DisString_EN(80, 45, "Select Number of Letters", &Font20, WHITE, RED);
    if (status_message[0]) GUI_DisString_EN(20, 75, status_message, &Font16, WHITE, RED);
    
    char selected_number_str[2];
    snprintf(selected_number_str, sizeof(selected_number_str), "%d", selected_number);
    GUI_DisString_EN(NUM_DIGIT_X0, NUM_DIGIT_Y0, selected_number_str, &Font24, WHITE, BLACK);

    menu_button->draw();
    num_select_start_button->draw();
    num_select_back_button->draw();
    num_select_next_button->draw();
}

void draw_send_data_screen(void) {
    LCD_Clear(LCD_BACKGROUND);
    GUI_DisString_EN(80, 45, "All Set!", &Font20, WHITE, BLACK);
    GUI_DisString_EN(80, 75, "Press Send to continue.", &Font20, WHITE, BLACK);

    rerecording_button->draw();
    send_button->draw();
}

void draw_ocr_result_screen(void) {
    LCD_Clear(LCD_BACKGROUND);

    if (!ocr_text[0]) {
        GUI_DisString_EN(80, 45, "Sending data...", &Font20, WHITE, BLACK);
        GUI_DisString_EN(80, 75, "Waiting for response...", &Font20, WHITE, BLACK);
        return;
    }

    GUI_DisString_EN(80, 45, "Data sent!", &Font20, WHITE, BLACK);

    char result[EVENT_TEXT_MAX + 16];
    snprintf(result, sizeof(result), "Predicted: %s", ocr_text);

    // Rectangle centered on the screen
    const int rect_x0 = (LCD_W - RESULT_TEXT_W) / 2;
    const int rect_y0 = (LCD_H - RESULT_TEXT_H) / 2;
    GUI_DrawRectangle(rect_x0, rect_y0,
                        rect_x0 + RESULT_TEXT_W, rect_y0 + RESULT_TEXT_H,
                        GREEN, DRAW_EMPTY, DOT_PIXEL_2X2);

    // Text centered inside the rectangle
    const int text_w = (int)strlen(result) * Font20.Width;
    const int text_x = rect_x0 + (RESULT_TEXT_W - text_w) / 2;
    const int text_y = rect_y0 + (RESULT_TEXT_H - Font20.Height) / 2;
    GUI_DisString_EN(text_x, text_y, result, &Font20, WHITE, BLACK);

    back_to_default_screen_button->draw();
}

void draw_recording_screen(void) {
    LCD_Clear(LCD_BACKGROUND);
    GUI_DisString_EN(80, 45, "Recording Data...", &Font20, WHITE, RED);
    GUI_DisString_EN(80, 75, "Hold still, then write.", &Font16, WHITE, BLACK);
}

// Capture prompt, called by core 0 between characters while core 1 is idle. `line` is drawn without clearing
// the screen (padded, so the previous text is overwritten); `clear` redraws the whole prompt first.
void APP_ShowCapturePrompt(uint8_t index, uint8_t count, const char* line)
{
    char text[40];
    LCD_Clear(LCD_BACKGROUND);
    GUI_DisString_EN(80, 45, "Recording Data...", &Font20, WHITE, RED);
    snprintf(text, sizeof text, "Letter %u of %u", index + 1, count);
    GUI_DisString_EN(80, 75, text, &Font20, WHITE, BLACK);
    snprintf(text, sizeof text, "%-22s", line);
    GUI_DisString_EN(80, 105, text, &Font20, WHITE, BLACK);
}

void draw_menu_screen(void) {
    LCD_Clear(LCD_BACKGROUND);
    back_button->draw();
    logout_button->draw();
}

void on_send_button_pressed(void)
{
    if (prevent_interaction()) return;
    printf("Send button pressed.\n");

    // Trigger a http request on the worker
    if (!send_command(CMD_SEND_WORD, inference->number_of_letters, next_session_id - 1)) {
        printf("Command queue is full, try again.\n");
        return;
    }
    ocr_text[0] = 0;
    screen_state = OCR_RESULT;
    draw_ocr_result_screen();
}

void on_menu_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Menu button pressed.\n");
    screen_state = MENU_OPEN;
    draw_menu_screen();
}

void on_back_button_pressed(void)
{
    if (prevent_interaction()) return;
    printf("Back button pressed.\n");

    screen_state = NUM_SELECT;
    draw_initial_screen();
}

void on_logout_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Logging out...\n");
    LCD_Clear(LCD_BACKGROUND);
    int screen_center_x = LCD_W / 2;
    int screen_center_y = LCD_H / 2;
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y, "Logged out!", &Font24, WHITE, BLACK);
    GUI_DisString_EN(screen_center_x - (screen_center_x / 2), screen_center_y + 26, "Goodbye!", &Font20, WHITE, BLACK);
    sleep_ms(1000);

    // push a flag to the fifo queue
    // multicore_fifo_push_blocking(CORE1_EXIT_FLAG);
}

void on_back_to_default_screen_pressed(void) {
    if (prevent_interaction()) return;

    printf("Back to default screen button pressed.\n");
    screen_state = NUM_SELECT;
    draw_initial_screen();
}

void on_num_select_start_button_pressed(void)
{
    if (prevent_interaction()) return;

    printf("Start button pressed.\n");
    status_message[0] = 0;
    if (!send_command(CMD_START_CAPTURE, selected_number, next_session_id)) {
        printf("Command queue is full, try again.\n");
        return;
    }
    next_session_id++;
    screen_state = RECORDING;
    inference->number_of_letters = selected_number;
    draw_recording_screen();
}

void on_num_select_next_button_pressed(void)
{
    if (prevent_interaction()) return;
    selected_number++;
    if (selected_number > MAX_CHAR_COUNTS) selected_number = 1;
    draw_initial_screen();
}

void on_num_select_back_button_pressed(void)
{
    if (prevent_interaction()) return;
    selected_number--;
    if (selected_number < 1) selected_number = MAX_CHAR_COUNTS;
    draw_initial_screen();
}


void on_rerecording_button_pressed(void) {
    if (prevent_interaction()) return;

    printf("Rerecording button pressed.\n");
    screen_state = NUM_SELECT;
    draw_initial_screen();
}

void TP_DrawBoard(void)
{
    if (prevent_interaction()) return;

    TP_Scan(0);
    if (sTP_DEV.chStatus & TP_PRESS_DOWN)
    { 
        spi_init(SPI_PORT, 10000000);
        sTP_Draw.Color = BLACK;

        // printf("horizontal x:%d,y:%d\n", sTP_Draw.Xpoint, sTP_Draw.Ypoint);
        if (screen_state == MENU_OPEN) {
            if (back_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Back button pressed\n");
                back_button->call_cb();
            }
            else if (logout_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Logout button pressed\n");
                logout_button->call_cb();
            }
        }
        else if (screen_state == NUM_SELECT) {
            if (menu_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Menu button pressed\n");
                menu_button->call_cb();
            } else if (num_select_back_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Back button pressed\n");
                num_select_back_button->call_cb();
            } else if (num_select_next_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Next button pressed\n");
                num_select_next_button->call_cb();
            } else if (num_select_start_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Start button pressed\n");
                num_select_start_button->call_cb();
            }
        }
        else if (screen_state == SEND_DATA) {
            if (rerecording_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint)) {
                printf("Rerecording button pressed\n");
                rerecording_button->call_cb();
            } else if (send_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint)) {
                printf("Send button pressed\n");
                send_button->call_cb();
            }
        }
        else if (screen_state == OCR_RESULT) {
            // Handle OCR result-specific touch events here if needed
            if (back_to_default_screen_button->is_pressed(sTP_Draw.Xpoint, sTP_Draw.Ypoint))
            {
                printf("Back to default screen button pressed\n");
                back_to_default_screen_button->call_cb();
            }
        }

        spi_init(SPI_PORT, 5000000);
    }
    SPI4W_Write_Byte(0xFF);
}


// Drains the events posted by the worker and updates the screen.
void APP_Poll(void)
{
    AppEvent e;
    for (int i = 0; i < 16 && queue_try_remove(&event_queue, &e); i++) {
        switch (e.type) {
            case EVT_ALL_SAVED:
                screen_state = SEND_DATA;
                draw_send_data_screen();
                break;
            case EVT_OCR_RESULT:
                snprintf(ocr_text, sizeof ocr_text, "%s", e.text);
                screen_state = OCR_RESULT;
                draw_ocr_result_screen();
                break;
            case EVT_ERROR:
                printf("Worker error: stage %u, code %u\n", e.stage, e.error);
                if (e.stage == STAGE_UPLOAD && e.text[0]) {
                    snprintf(status_message, sizeof status_message, "Server: %s (letter %ld)", e.text, (long)e.detail + 1);
                } else {
                    snprintf(status_message, sizeof status_message, "%s (%ld)",
                             e.stage == STAGE_SAVE ? "SD save failed" :
                             e.stage == STAGE_UPLOAD ? "Upload failed" : "Error", (long)e.detail);
                }
                screen_state = NUM_SELECT;
                draw_initial_screen();
                break;
            default:
                break;
        }
    }
}

void APP_Init(LCD_SCAN_DIR lcd_scan_dir, OCR_Inference* _inference)
{
    app_queues_init();

    LCD_Init(lcd_scan_dir, 1000);
    TP_Init(lcd_scan_dir, &sTP_DEV, &sTP_Draw);

    TP_GetAdFac();

    inference = _inference;
    inference->state = DEFAULT;

    // IMU Recording starter screen buttons
    num_select_start_button = new Button(START_IMU_X0, START_IMU_Y0, LONG_BUTTON_W, BUTTON_H, "START RECORDING", DOT_PIXEL_2X2, BLUE, WHITE, 20, 13);
    num_select_start_button->set_callback(on_num_select_start_button_pressed);
    num_select_next_button = new Button(NUM_SELECT_NEXT_X0, NUM_SELECT_NEXT_Y0, SHORT_BUTTON_W, BUTTON_H, "+1", DOT_PIXEL_2X2, BLACK, WHITE, 22, 13);
    num_select_next_button->set_callback(on_num_select_next_button_pressed);
    num_select_back_button = new Button(NUM_SELECT_BACK_X0, NUM_SELECT_BACK_Y0, SHORT_BUTTON_W, BUTTON_H, "-1", DOT_PIXEL_2X2, BLACK, WHITE, 30, 13);
    num_select_back_button->set_callback(on_num_select_back_button_pressed);

    // Screen after IMU recording
    rerecording_button = new Button(RERECORD_X0, RERECORD_Y0, BUTTON_W + (4*MARGIN), BUTTON_H, "RERECORD", DOT_PIXEL_2X2, RED, WHITE, 20, 13);
    rerecording_button->set_callback(on_rerecording_button_pressed);
    send_button = new Button(SEND_X0, SEND_Y0, BUTTON_W, BUTTON_H, "SEND", DOT_PIXEL_2X2, BLUE, WHITE, 22, 13);
    send_button->set_callback(on_send_button_pressed);

    // OCR result screen
    back_to_default_screen_button = new Button(BACK_TO_DEFAULT_SCREEN_X0, BACK_TO_DEFAULT_SCREEN_Y0, BUTTON_W, BUTTON_H, "BACK", DOT_PIXEL_2X2, BLUE, WHITE, 22, 13);
    back_to_default_screen_button->set_callback(on_back_to_default_screen_pressed);

    // IMU Recording menu buttons
    menu_button = new Button(MENU_X0, MENU_Y0, 60, BUTTON_H, "MENU", DOT_PIXEL_2X2, GREEN, WHITE, 4, 13);
    menu_button->set_callback(on_menu_button_pressed);
    back_button = new Button(BACK_X0, BACK_Y0, MENU_BUTTON_W, BUTTON_H, "BACK", DOT_PIXEL_2X2, BLACK, WHITE, 30, 13);
    back_button->set_callback(on_back_button_pressed);
    logout_button = new Button(LOGOUT_X0, LOGOUT_Y0, MENU_BUTTON_W, BUTTON_H, "LOGOUT", DOT_PIXEL_2X2, RED, WHITE, 24, 13);
    logout_button->set_callback(on_logout_button_pressed);

    draw_initial_screen();
}
