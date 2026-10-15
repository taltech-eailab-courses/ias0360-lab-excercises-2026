#include <string.h>
#include <stdio.h>
#include <cstring>
#include "LCD_utils.hpp"

Button::Button(int x, int y, int w, int h, const char* label,
               DOT_PIXEL font_size, 
               hex_color_t bg_color, 
               hex_color_t fg_color,
               uint8_t letter_offset_x, uint8_t letter_offset_y)
    : x_(x), y_(y), w_(w), h_(h),
      label_(label), font_size_(font_size),
      bg_color_(bg_color), fg_color_(fg_color),
      letter_offset_x_(letter_offset_x), letter_offset_y_(letter_offset_y)
{
}

Button::~Button() {
    callback_ = nullptr;
}

void Button::set_callback(void (*cb)()) {
    callback_ = cb;
}

bool Button::is_pressed(int tx, int ty) {
    bool is_pressed = (tx >= x_ && tx < (x_ + w_) &&
            ty >= y_ && ty < (y_ + h_));
    if (!is_pressed) return false;

    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - button_pressed_at_ < IGNORE_INTERVAL_MS) {
        // prevent debounce
        return false;
    }
    button_pressed_at_ = current_time;
    return true;
}

void Button::draw() {
    GUI_DrawRectangle(x_, y_,
              x_ + w_, y_ + h_,
              bg_color_, DRAW_EMPTY, font_size_);
    GUI_DisString_EN(x_ + letter_offset_x_, y_ + letter_offset_y_,
                label_, &Font20, fg_color_, bg_color_);
}
void Button::call_cb() {
    if (callback_) {
        callback_();
    }
}


void TextField::clear_data(void) {
    std::fill(binary_data_.begin(), binary_data_.end(), 0);
}

void TextField::draw_box(void) {
    GUI_DrawRectangle(x0_, y0_,
                        x1_, y1_,
                        BLACK, DRAW_EMPTY, DOT_PIXEL_2X2);
}

void TextField::set_pixel(POINT tx, POINT ty) {
    if (binary_data_.empty()) return;

    int x = tx - x0_;
    int y = ty - y0_;
    if (x >= 0 && x < box_w_ && y >= 0 && y < box_h_) {
        binary_data_[y * box_w_ + x] = 1;
    }
}

bool TextField::is_box_touched(int tx, int ty) {
    return (tx >= x0_ && tx < x1_ &&
            ty >= y0_ && ty < y1_);
}

void TextField::print_data(void) {
    for (int i=0; i<box_h_; i++) {
        for (int j=0; j<box_w_; j++) {
            printf("%d ", static_cast<int>(binary_data_[i * box_w_ + j]));
        }
        printf("\n");
    }
}