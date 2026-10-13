// Full-screen alarm takeovers (design artboards 4 "Warning alarm",
// 5 "Critical alarm" and 6 "Chimney fire").
#pragma once

#include <lvgl.h>

#include "settings.h"
#include "state.h"

namespace ui::alarm_screens {

enum class Kind { Warning, Critical, ChimneyFire };

void create();
lv_obj_t* screen(Kind k);
// probe: the channel that raised the alarm (ignored for ChimneyFire = FLUE).
void update(Kind k, int probe, const state::Snapshot& s, const settings::Settings& cfg);

}  // namespace ui::alarm_screens
