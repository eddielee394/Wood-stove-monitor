// Stove monitor - WT32-SC01 Plus
// Phase 3: core firmware. Sensor and alarm tasks run independently of this
// loop, which only draws an interim status screen (LVGL arrives in Phase 4)
// and services the serial console.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "alarms.h"
#include "buzzer.h"
#include "console.h"
#include "display.h"
#include "sensors.h"
#include "settings.h"
#include "state.h"

LGFX tft;
static LGFX_Sprite screen(&tft);  // full-frame buffer in PSRAM, no flicker

static const uint32_t WDT_TIMEOUT_S = 10;
static const uint32_t UI_PERIOD_MS = 250;

using state::Level;

static uint16_t levelColour(Level l) {
  switch (l) {
    case Level::Critical: return TFT_RED;
    case Level::Fault: return TFT_MAGENTA;
    case Level::Warning: return TFT_ORANGE;
    case Level::Reload: return TFT_BLUE;
    default: return TFT_DARKGREEN;
  }
}

static float display(float f, bool c) { return c ? (f - 32.0f) * 5.0f / 9.0f : f; }

static void drawBanner(const state::Snapshot& s) {
  const state::Alarms& a = s.alarms;
  uint16_t bg = a.highest == Level::None ? TFT_DARKGREY : levelColour(a.highest);
  screen.fillRect(0, 0, 480, 48, bg);
  screen.setTextColor(TFT_WHITE, bg);
  screen.setFont(&fonts::Font4);
  screen.setTextDatum(middle_left);

  const char* msg = "All normal";
  if (a.rateHigh) msg = "FLUE RISING FAST";
  else if (a.highest == Level::Critical) msg = "CRITICAL TEMPERATURE";
  else if (a.highest == Level::Fault) msg = "PROBE FAULT";
  else if (a.highest == Level::Warning) msg = "HIGH TEMPERATURE";
  else if (a.highest == Level::Reload) msg = "Time to reload";
  screen.drawString(msg, 12, 24);

  if (a.highest != Level::None) {
    screen.setFont(&fonts::Font2);
    screen.setTextDatum(middle_right);
    screen.drawString(a.silenced ? "silenced" : "tap to silence", 470, 24);
  }
}

static void drawChannel(const state::Snapshot& s, int i, bool unitsC) {
  const state::Channel& c = s.ch[i];
  const int x = 4 + i * 240, y = 54, w = 232, h = 236;
  Level lvl = s.alarms.fault[i] ? Level::Fault : s.alarms.temp[i];
  if (i == thermo::FLUE && s.alarms.rateHigh) lvl = Level::Critical;
  uint16_t frame = levelColour(lvl);

  screen.fillRoundRect(x, y, w, h, 10, frame);
  screen.fillRoundRect(x + 4, y + 4, w - 8, h - 8, 8, TFT_BLACK);
  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.setTextDatum(top_left);
  screen.setFont(&fonts::Font4);
  screen.drawString(thermo::name((thermo::Channel)i), x + 14, y + 12);

  if (!c.valid) {
    screen.setTextColor(TFT_MAGENTA, TFT_BLACK);
    screen.drawString(c.present ? "PROBE FAULT" : "NO SENSOR", x + 14, y + 80);
    if (c.present && c.fault) {
      char faults[48];
      thermo::describeFault(c.fault, faults, sizeof(faults));
      screen.setFont(&fonts::Font2);
      screen.drawString(faults, x + 14, y + 115);
    }
    return;
  }

  char buf[24];
  screen.setFont(&fonts::Font7);
  screen.setTextSize(1.4f);
  snprintf(buf, sizeof(buf), "%.0f", display(c.tempF, unitsC));
  screen.drawString(buf, x + 14, y + 55);
  int tw = screen.textWidth(buf);
  screen.setTextSize(1);
  screen.setFont(&fonts::Font4);
  screen.drawString(unitsC ? "C" : "F", x + 20 + tw, y + 55);

  // Rate of change, with an arrow once it is more than a trickle
  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (isnan(c.rateFpm)) {
    screen.drawString("rate: --", x + 14, y + 140);
  } else {
    float r = unitsC ? c.rateFpm * 5.0f / 9.0f : c.rateFpm;
    int ax = x + 24, ay = y + 152;
    if (r >= 2) screen.fillTriangle(ax - 10, ay + 8, ax + 10, ay + 8, ax, ay - 10, TFT_ORANGE);
    else if (r <= -2) screen.fillTriangle(ax - 10, ay - 8, ax + 10, ay - 8, ax, ay + 10, TFT_CYAN);
    snprintf(buf, sizeof(buf), "%+.0f /min", r);
    screen.drawString(buf, x + 44, y + 140);
  }

  screen.setFont(&fonts::Font2);
  snprintf(buf, sizeof(buf), "board %.0f%s", display(c.boardF, unitsC), unitsC ? "C" : "F");
  screen.drawString(buf, x + 14, y + 200);
}

static void drawStatus(const state::Snapshot& s) {
  char buf[64];
  uint32_t age = s.sensorMs ? millis() - s.sensorMs : 0;
  screen.setFont(&fonts::Font2);
  screen.setTextDatum(top_left);
  screen.setTextColor(age > 3000 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  snprintf(buf, sizeof(buf), "sensors %.1fs ago   up %lum", age / 1000.0f,
           (unsigned long)(millis() / 60000));
  screen.drawString(buf, 8, 300);

  if (s.alarms.ideal) {
    screen.fillRoundRect(352, 294, 124, 24, 12, TFT_DARKGREEN);
    screen.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    screen.setTextDatum(middle_center);
    screen.drawString("IDEAL BURN", 414, 306);
  }
}

static void drawScreen() {
  state::Snapshot s = state::get();
  bool unitsC = settings::get().unitsC;
  screen.fillScreen(TFT_BLACK);
  drawBanner(s);
  for (int i = 0; i < thermo::COUNT; i++) drawChannel(s, i, unitsC);
  drawStatus(s);
  screen.pushSprite(0, 0);
}

void setup() {
  buzzer::begin();  // pin held quiet before anything else
  Serial.begin(115200);
  delay(500);
  Serial.println("Stove monitor starting");

  // Reboot if the sensor or alarm task stops checking in.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);

  settings::begin();
  state::begin();
  sensors::start();
  alarms::start();

  tft.init();
  tft.setRotation(1);  // landscape, 480x320
  tft.setBrightness(settings::get().brightness);
  screen.setPsram(true);
  screen.setColorDepth(16);
  if (!screen.createSprite(480, 320)) Serial.println("UI: sprite allocation failed");
  Serial.println("Type 'help' for console commands");
}

void loop() {
  static uint32_t lastDraw = 0;
  static int brightness = -1;
  static bool touching = false;

  console::poll();

  // Any new touch acknowledges/silences the current alarm.
  int32_t x, y;
  bool touched = tft.getTouch(&x, &y);
  if (touched && !touching) {
    Serial.printf("touch x=%ld y=%ld -> silence\n", (long)x, (long)y);
    state::requestSilence();
  }
  touching = touched;

  if (millis() - lastDraw >= UI_PERIOD_MS) {
    lastDraw = millis();
    int b = settings::get().brightness;
    if (b != brightness) tft.setBrightness(brightness = b);
    drawScreen();
  }
  delay(10);
}
