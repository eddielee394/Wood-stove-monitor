// Stove monitor - WT32-SC01 Plus
// Phase 4: LVGL UI on top of the Phase 3 core. Sensor and alarm tasks run independently of this
// loop, which runs the LVGL UI and the serial console.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "alarms.h"
#include "buzzer.h"
#include "console.h"
#include "display.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "state.h"
#include "ui/ui.h"
#include "weather.h"

LGFX tft;

static const uint32_t WDT_TIMEOUT_S = 10;

void setup() {
  buzzer::begin();  // pin held quiet before anything else
  Serial.begin(115200);
  delay(500);
  Serial.printf("Stove monitor starting (last reset: %s)\n", state::resetReason());

  // Reboot if the sensor or alarm task stops checking in.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);

  settings::begin();
  state::begin();
  sensors::start();
  alarms::start();
  net::start();  // after the safety tasks; nothing depends on it
  weather::start();

  tft.init();
  tft.setRotation(1);  // landscape, 480x320
  ui::begin();
  Serial.println("Type 'help' for console commands");
}

void loop() {
  console::poll();
  ui::loop();
  delay(5);
}
