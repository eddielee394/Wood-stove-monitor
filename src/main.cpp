// Stove monitor - Phase 1: buzzer driver on the WT32-SC01 Plus
// Touch buttons play the buzzer driver's alarm patterns for tuning by ear.

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "buzzer.h"

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
// Phase 1 buzzer pattern demo. Tap a pattern to start it; repeating patterns
// keep going until STOP or another pattern is tapped.
// ---------------------------------------------------------------------------
struct Button { const char* label; buzzer::Pattern pattern; };

static const Button buttons[] = {
    {"Chirp",    buzzer::Pattern::Chirp},
    {"Warning",  buzzer::Pattern::Warning},
    {"Critical", buzzer::Pattern::Critical},
    {"Fault",    buzzer::Pattern::Fault},
    {"STOP",     buzzer::Pattern::None},
};
static const int N_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);
static const int BTN_W = 150, BTN_H = 100, BTN_GAP = 10, BTN_TOP = 80;

static void buttonRect(int i, int& x, int& y) {
  x = BTN_GAP + (i % 3) * (BTN_W + BTN_GAP);
  y = BTN_TOP + (i / 3) * (BTN_H + BTN_GAP);
}

static void drawButtons() {
  buzzer::Pattern now = buzzer::current();
  for (int i = 0; i < N_BUTTONS; i++) {
    int x, y;
    buttonRect(i, x, y);
    bool active = buttons[i].pattern != buzzer::Pattern::None &&
                  buttons[i].pattern == now;
    uint16_t colour = buttons[i].pattern == buzzer::Pattern::None ? TFT_RED
                      : active ? TFT_ORANGE : TFT_DARKGREY;
    tft.fillRoundRect(x, y, BTN_W, BTN_H, 10, colour);
    tft.setTextColor(TFT_WHITE, colour);
    tft.setTextSize(2);
    tft.setCursor(x + 14, y + BTN_H / 2 - 8);
    tft.print(buttons[i].label);
  }
}

static void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Buzzer patterns");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.print("Tap a pattern. Orange = playing. STOP silences.");
  drawButtons();
}

void setup() {
  buzzer::begin();
  Serial.begin(115200);
  delay(500);
  Serial.println("Stove monitor buzzer pattern demo starting");

  tft.init();
  tft.setRotation(1);       // landscape, 480x320
  tft.setBrightness(200);   // 0-255
  drawScreen();
}

void loop() {
  static buzzer::Pattern shown = buzzer::Pattern::None;

  int32_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    for (int i = 0; i < N_BUTTONS; i++) {
      int x, y;
      buttonRect(i, x, y);
      if (tx >= x && tx < x + BTN_W && ty >= y && ty < y + BTN_H) {
        Serial.printf("Pattern: %s\n", buttons[i].label);
        buzzer::play(buttons[i].pattern);
        delay(300);  // ignore the finger still on the glass
        break;
      }
    }
  }

  // Redraw when a pattern starts or ends (Chirp ends by itself)
  buzzer::Pattern now = buzzer::current();
  if (now != shown) {
    shown = now;
    drawButtons();
  }
  delay(10);
}
