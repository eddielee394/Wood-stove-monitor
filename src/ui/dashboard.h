// Main dashboard (design artboards 1 "Main dashboard" and 3 "Probe fault").
#pragma once

#include <lvgl.h>

#include "settings.h"
#include "state.h"

namespace ui::dashboard {

lv_obj_t* create(void (*onSettings)());
void update(const state::Snapshot& s, const settings::Settings& cfg);

}  // namespace ui::dashboard
