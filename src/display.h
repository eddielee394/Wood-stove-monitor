// WT32-SC01 Plus display + touch configuration for LovyanGFX.
// Verified on hardware in Phase 0: landscape upright, colours correct with
// invert=true, touch aligned.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

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

extern LGFX tft;
