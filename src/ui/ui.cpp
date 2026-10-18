#include "ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "alarm_screens.h"
#include "dashboard.h"
#include "ota.h"
#include "settings_screen.h"
#include "wifi_setup.h"
#include "display.h"
#include "settings.h"
#include "state.h"
#include "theme.h"

namespace ui {
namespace {

const int W = 480, H = 320;
const int BUF_LINES = 40;
const uint32_t UPDATE_MS = 250;

lv_display_t* disp;
lv_obj_t* dashScreen;
lv_obj_t* settingsScreen;
lv_obj_t* shown;
bool settingsOpen = false;
bool passwordOpen = false;
uint32_t lastTouchMs = 0;
const uint32_t SETTINGS_IDLE_MS = 60000;  // back to the dashboard when idle
int brightness = -1;
int flipped = -1;

void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  // Swap to the panel's byte order here and send the pixels untouched, so
  // LovyanGFX never converts inside LVGL's buffer; endWrite() returns only
  // when the transfer is done, after which LVGL may reuse the buffer.
  uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
  lv_draw_sw_rgb565_swap(px, w * h);
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels(reinterpret_cast<const uint16_t*>(px), w * h, false);
  tft.endWrite();
  lv_display_flush_ready(d);
}

void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  int32_t x, y;
  if (tft.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    lastTouchMs = millis();
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void refresh();
void openSettings() {
  settingsOpen = true;
  lastTouchMs = millis();
}
void closeSettings() { settingsOpen = false; }

lv_obj_t* otaBox;
lv_obj_t* otaLabel;

void updateOta() {
  int p = ota::progress();
  if (p < 0) {
    show(otaBox, false);
    return;
  }
  show(otaBox, true);
  lv_label_set_text_fmt(otaLabel, "Updating firmware… %d%%", p);
}

void refresh() {
  updateOta();
  state::Snapshot s = state::get();
  settings::Settings cfg = settings::get();
  if (cfg.brightness != brightness) tft.setBrightness(brightness = cfg.brightness);
  if (cfg.flip != flipped) {
    // Landscape either way up: rotation 1 normal, 3 turned 180°. LovyanGFX
    // rotates touch to match; redraw everything in the new orientation.
    tft.setRotation((flipped = cfg.flip) ? 3 : 1);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
  }
  // Pick the screen: an unsilenced temperature alarm takes over the whole
  // display; everything else (faults, reload, silenced alarms) stays on the
  // dashboard with its own strip, banner or chip.
  using alarm_screens::Kind;
  const state::Alarms& a = s.alarms;
  if (settingsOpen && millis() - lastTouchMs > SETTINGS_IDLE_MS) {
    settingsOpen = false;
  }
  if (!settingsOpen) passwordOpen = false;
  lv_obj_t* want = !settingsOpen  ? dashScreen
                   : passwordOpen ? wifi_setup::passwordScreen()
                                  : settingsScreen;
  if (!a.silenced) {
    int probe = -1;
    Kind kind = Kind::Warning;
    if (a.rateHigh) {
      kind = Kind::ChimneyFire;
      probe = thermo::FLUE;
    } else {
      for (state::Level lvl : {state::Level::Critical, state::Level::Warning}) {
        for (int i = thermo::COUNT - 1; i >= 0 && probe < 0; i--) {
          if (a.temp[i] == lvl) {
            probe = i;
            kind = lvl == state::Level::Critical ? Kind::Critical : Kind::Warning;
          }
        }
      }
    }
    if (probe >= 0) {
      alarm_screens::update(kind, probe, s, cfg);
      want = alarm_screens::screen(kind);
    }
  }
  if (want == dashScreen) dashboard::update(s, cfg);
  if (want == settingsScreen) settings_screen::update(cfg);
  if (want != shown) {
    shown = want;
    lv_screen_load(want);
  }
}

}  // namespace

void begin() {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  disp = lv_display_create(W, H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  size_t bytes = W * BUF_LINES * 2;
  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, readTouch);

  dashScreen = dashboard::create([]() {
    openSettings();
    refresh();  // switch now rather than on the next tick
  });
  settingsScreen = settings_screen::create(
      []() {
        closeSettings();
        refresh();
      },
      []() {
        passwordOpen = true;
        refresh();
      },
      []() {
        passwordOpen = false;
        refresh();
      });
  alarm_screens::create();
  // OTA progress sits on the top layer, over whichever screen is showing.
  otaBox = box(lv_layer_top());
  lv_obj_set_size(otaBox, 360, 72);
  lv_obj_center(otaBox);
  lv_obj_set_style_radius(otaBox, 12, 0);
  lv_obj_set_style_bg_color(otaBox, c(ACCENT), 0);
  lv_obj_set_style_bg_opa(otaBox, LV_OPA_COVER, 0);
  otaLabel = label(otaBox, &jakarta_20_xb, TEXT, "");
  lv_obj_center(otaLabel);
  show(otaBox, false);
  refresh();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= UPDATE_MS) {
    last = millis();
    refresh();
  }
  lv_timer_handler();
}

}  // namespace ui
