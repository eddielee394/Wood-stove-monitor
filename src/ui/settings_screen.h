// Settings (design artboards 7 "Settings · limits" and 8 "Settings · alerts").
// Every change goes through settings::apply, the same validated setter the
// console and (later) web/MQTT use, and is saved immediately.
#pragma once

#include <lvgl.h>

#include "settings.h"

namespace ui::settings_screen {

lv_obj_t* create(void (*onBack)());
void update(const settings::Settings& cfg);

}  // namespace ui::settings_screen
