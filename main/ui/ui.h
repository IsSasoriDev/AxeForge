#pragma once

// AxeForge screen UI (LVGL 8, 480x320). build/on_* run on the display task;
// the request_* calls are safe from any task (applied on the next refresh).
namespace ui {
void build();
void on_button(int index);      // 0 = next page, 1 = previous page
void on_long_press(int index);  // 0 = backlight on/off, 1 = restart auto-tune

void request_page(int delta);   // +1 / -1
void request_kind(char kind);   // jump to a page by letter (d c b p x s u v)
void reload_pages();            // settings.pages changed
int current_page();
char current_kind();
}
