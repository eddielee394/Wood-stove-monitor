// Stove monitor - step 1: display + touch test for the WT32-SC01 Plus
// Draws a test screen, shows touch coordinates, and paints where you touch.

#define LGFX_USE_V1
#include <Arduino.h>
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

static LGFX tft;

// "Clear" button area (top-right corner, landscape)
static const int BTN_X = 380, BTN_Y = 0, BTN_W = 100, BTN_H = 40;
// "Invert" toggle button, left of CLEAR - for colour diagnosis
static const int INV_X = 270, INV_W = 100;
static bool inverted = true;  // matches cfg.invert above

void drawTestScreen() {
  tft.fillScreen(TFT_BLACK);

  // Numbered colour bars - expected colour printed under each one
  struct Bar { uint16_t c; const char* name; };
  const Bar bars[] = {
      {TFT_RED, "RED"},     {TFT_GREEN, "GREEN"}, {TFT_BLUE, "BLUE"},
      {TFT_WHITE, "WHITE"}, {TFT_YELLOW, "YELLOW"}, {0x8410, "GREY"},
  };
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 6; i++) {
    tft.fillRect(i * 80 + 4, 200, 72, 70, bars[i].c);
    tft.setCursor(i * 80 + 8, 276);
    tft.printf("%d %s", i + 1, bars[i].name);
  }
  tft.setCursor(10, 300);
  tft.printf("invert=%s", inverted ? "ON" : "OFF");

  tft.fillRoundRect(INV_X, BTN_Y + 4, INV_W - 4, BTN_H - 8, 6, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(INV_X + 12, BTN_Y + 13);
  tft.print("INVERT");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Stove monitor - display test");

  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.printf("PSRAM: %u KB   Flash: %u MB",
             ESP.getPsramSize() / 1024, ESP.getFlashChipSize() / (1024 * 1024));
  tft.setCursor(10, 55);
  tft.println("Touch the screen to draw. Tap CLEAR to reset.");

  tft.fillRoundRect(BTN_X, BTN_Y + 4, BTN_W - 4, BTN_H - 8, 6, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(BTN_X + 18, BTN_Y + 13);
  tft.print("CLEAR");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Stove monitor display test starting");

  tft.init();
  tft.setRotation(1);       // landscape, 480x320
  tft.setBrightness(200);   // 0-255
  drawTestScreen();

  Serial.printf("Screen: %dx%d  PSRAM: %u bytes\n",
                tft.width(), tft.height(), ESP.getPsramSize());
}

void loop() {
  int32_t x, y;
  if (tft.getTouch(&x, &y)) {
    if (x >= BTN_X && y < BTN_Y + BTN_H) {
      drawTestScreen();
      delay(250);  // simple debounce
      return;
    }
    if (x >= INV_X && x < INV_X + INV_W && y < BTN_Y + BTN_H) {
      inverted = !inverted;
      tft.invertDisplay(inverted);
      Serial.printf("invert=%d\n", inverted);
      drawTestScreen();
      delay(400);
      return;
    }
    tft.fillCircle(x, y, 4, TFT_ORANGE);

    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 180);
    tft.printf("Touch: x=%3d y=%3d   ", x, y);

    Serial.printf("Touch x=%d y=%d\n", x, y);
  }
  delay(10);
}