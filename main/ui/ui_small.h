#pragma once

// Compact UI for the LilyGo T-Display-S3 (320x170), the NerdQaxe++ stock
// screen. Same pages as the 3.5" UI minus the custom layouts.
namespace ui_small {
void build();
void on_button(int index);
void on_long_press(int index);
void request_page(int delta);
void request_kind(char kind);
void reload_pages();
char current_kind();
}
