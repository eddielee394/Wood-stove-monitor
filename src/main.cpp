// Stove monitor - Phase 2: thermocouple bench test on the WT32-SC01 Plus
// Reads both MAX31856 channels once a second; shows temps and faults.

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "buzzer.h"
#include "thermo.h"

// ---------------------------------------------------------------------------
// Display configuration (pins from the WT32-SC01 Plus datasheet)
// ST7796 panel on an 8-bit parallel bus, FT6336 touch (FT5x06 family)
// ---------------------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_Parallel8 _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_FT5x06  _touch;

public:
  LGFX() {
    {  // Parallel bus
      auto cfg = _bus.config();
      cfg.freq_write = 20000000;
      cfg.pin_wr = 47;
      cfg.pin_rd = -1;
      cfg.pin_rs = 0;   // command/data select
      cfg.pin_d0 = 9;
      cfg.pin_d1 = 46;
      cfg.pin_d2 = 3;
      cfg.pin_d3 = 8;
      cfg.pin_d4 = 18;
      cfg.pin_d5 = 17;
      cfg.pin_d6 = 16;
      cfg.pin_d7 = 15;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {  // Panel
      auto cfg = _panel.config();
      cfg.pin_cs = -1;
      cfg.pin_rst = 4;  // shared with touch reset
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {  // Backlight
      auto cfg = _light.config();
      cfg.pin_bl = 45;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {  // Touch
      auto cfg = _touch.config();
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 6;
      cfg.pin_scl = 5;
      cfg.pin_int = 7;
      cfg.pin_rst = -1;  // reset handled by the panel (GPIO4)
      cfg.freq = 400000;
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;

// ---------------------------------------------------------------------------
// Phase 2 thermocouple bench test: both channels on screen and serial, 1Hz.
// ---------------------------------------------------------------------------
static float toF(float c) { return c * 9.0f / 5.0f + 32.0f; }

static void drawChannel(thermo::Channel ch, const thermo::Reading& r) {
  const int x = ch * 240, w = 240, y = 40, h = 280;
  bool ok = r.present && r.fault == 0 && !isnan(r.tcC);
  uint16_t frame = ok ? TFT_DARKGREEN : TFT_RED;

  tft.fillRect(x + 4, y, w - 8, h - 4, TFT_BLACK);
  tft.drawRoundRect(x + 4, y, w - 8, h - 4, 10, frame);
  tft.drawRoundRect(x + 5, y + 1, w - 10, h - 6, 9, frame);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(x + 20, y + 16);
  tft.print(thermo::name(ch));

  tft.setCursor(x + 20, y + 80);
  if (!r.present) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(3);
    tft.print("NO CHIP");
  } else {
    tft.setTextColor(ok ? TFT_WHITE : TFT_RED, TFT_BLACK);
    tft.setTextSize(5);
    tft.printf("%.1f", toF(r.tcC));
    tft.setTextSize(2);
    tft.print(" F");
  }

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(x + 20, y + 170);
  if (r.present) tft.printf("Board %.1f F", toF(r.cjC));

  char faults[64];
  thermo::describeFault(r.fault, faults, sizeof(faults));
  tft.setCursor(x + 20, y + 210);
  tft.setTextColor(r.present && !r.fault ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.setTextSize(r.fault ? 1 : 2);
  tft.print(r.present ? faults : "check wiring/CS");
}

void setup() {
  buzzer::begin();  // holds the buzzer pin quiet
  Serial.begin(115200);
  delay(500);
  Serial.println("Stove monitor thermocouple bench test starting");

  tft.init();
  tft.setRotation(1);       // landscape, 480x320
  tft.setBrightness(200);   // 0-255
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Thermocouple bench test");

  thermo::begin();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 1000) {
    delay(10);
    return;
  }
  last = millis();

  for (int i = 0; i < thermo::COUNT; i++) {
    auto ch = (thermo::Channel)i;
    thermo::Reading r = thermo::read(ch);
    drawChannel(ch, r);

    char faults[64];
    thermo::describeFault(r.fault, faults, sizeof(faults));
    Serial.printf("%-5s present=%d tc=%.2fC (%.1fF) cj=%.2fC fault=0x%02X %s\n",
                  thermo::name(ch), r.present, r.tcC, toF(r.tcC), r.cjC,
                  r.fault, faults);
  }
}
