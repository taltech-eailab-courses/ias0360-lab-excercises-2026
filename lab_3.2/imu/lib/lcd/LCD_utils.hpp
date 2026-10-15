#ifndef HTTP_CLIENT_REQUEST_HPP_
#define HTTP_CLIENT_REQUEST_HPP_

#include <vector>
#include <cstdint>


#ifdef __cplusplus
extern "C" {
#endif

#include "LCD_Touch.h"
#include "LCD_GUI.h"

#ifdef __cplusplus
}
#endif


typedef uint32_t hex_color_t;

class Button {
    public:
        Button(int x, int y, int w, int h, const char* label, DOT_PIXEL font_size= DOT_PIXEL_2X2, 
               hex_color_t bg_color=BLUE, hex_color_t fg_color=WHITE, uint8_t letter_offset_x=0, uint8_t letter_offset_y=0);
        ~Button();

        void set_callback(void (*cb)());

        bool is_pressed(int tx, int ty);
        void draw();
        void call_cb();

    private:
        int x_;
        int y_;
        int w_;
        int h_;
        hex_color_t bg_color_;
        hex_color_t fg_color_;
        DOT_PIXEL font_size_;
        const char* label_;
        uint8_t letter_offset_x_ = 0;
        uint8_t letter_offset_y_ = 0;
        uint32_t button_pressed_at_ = 0;
        void (*callback_)() = nullptr;
};


class TextField {
    public:
        TextField() 
            : x0_(0), y0_(0), box_w_(0), box_h_(0), x1_(0), y1_(0) {}
        TextField(uint16_t x0, uint16_t y0, uint16_t box_w, uint16_t box_h) 
            : x0_(x0), y0_(y0), box_w_(box_w), box_h_(box_h) {
            x1_ = x0_ + box_w_;
            y1_ = y0_ + box_h_;
            binary_data_.assign(box_w_ * box_h_, 0);
            clear_data();
        }
        ~TextField() {
            binary_data_.clear();
        }

        void clear_data(void);
        void draw_box(void);
        bool is_box_touched(int tx, int ty);
        void set_pixel(POINT tx, POINT ty);
        void print_data(void);
        const uint8_t* data() const { return binary_data_.data(); }
        const uint16_t get_x() const { return x0_; }
        const uint16_t get_y() const { return y0_; }

    private:
        uint16_t x0_;
        uint16_t y0_;
        uint16_t x1_;
        uint16_t y1_;
        uint16_t box_w_;
        uint16_t box_h_;
        std::vector<uint8_t> binary_data_;
};

#endif // HTTP_CLIENT_REQUEST_HPP_