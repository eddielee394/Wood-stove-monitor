// On-device Wi-Fi setup: the Settings "Wi-Fi" tab (status, scan, network
// list, forget) and a password screen with an on-screen keyboard.
#pragma once

#include <lvgl.h>

namespace ui::wifi_setup {

// Builds the tab's content inside `parent` (the settings body area).
lv_obj_t* buildTab(lv_obj_t* parent, void (*openPassword)(), void (*closePassword)());
void updateTab();
lv_obj_t* passwordScreen();

}  // namespace ui::wifi_setup
