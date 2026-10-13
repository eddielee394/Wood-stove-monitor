// LVGL user interface. Runs entirely in the Arduino loop task; it only reads
// state snapshots and goes through settings::apply / state::requestSilence,
// so it can never block the sensor or alarm tasks.
#pragma once

namespace ui {

void begin();  // after tft.init()
void loop();   // call often from loop()

}  // namespace ui
