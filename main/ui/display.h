#pragma once
#include <stdint.h>
#include "lvgl.h"

// Layout size of the 3.5" UI (ui.cpp). The T-Display UI (ui_small.cpp) uses
// display::width()/height() instead.
#define LCD_H_RES 480
#define LCD_V_RES 320

enum class DisplayType : uint8_t { Big = 0, TDisplay = 1 };

// Parallel (i80) or SPI panel + LVGL port + the two board buttons. ui::build() and all LVGL
// calls run inside the display task.
namespace display {
void start();
DisplayType type();
bool small();          // T-Display-S3 (320x170)
int width();
int height();
void set_brightness(uint8_t pct);
void set_rotation(bool rotate_180);
// Grabs the current screen as RGB565 (LVGL byte-swapped). Any task; blocks
// until the display task has rendered it. Free with free_snapshot().
lv_img_dsc_t *snapshot(int timeout_ms);
void free_snapshot(lv_img_dsc_t *img);
}
