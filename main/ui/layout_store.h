#pragma once

// Custom screen layouts, stored as JSON in NVS:
// {"pages":[[{"t":"gauge","x":0,"y":0,"w":5,"h":5,"txt":""},...], [...]]}
// Grid is 12 x 8 cells over the 480x272 page area.
namespace layout_store {

constexpr int COLS = 12;
constexpr int ROWS = 8;
constexpr int PAGES = 2;

const char *json();              // current layout (owned by the store)
bool set_json(const char *json); // validates, saves, marks dirty
bool take_dirty();               // UI thread: rebuild custom pages if true
void load();

} // namespace layout_store
