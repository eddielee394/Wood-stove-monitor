#include "ui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "dashboard.h"
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
int brightness = -1;

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
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void openSettings() {
  // Settings screens arrive in a later stage.
}

void refresh() {
  state::Snapshot s = state::get();
  settings::Settings cfg = settings::get();
  if (cfg.brightness != brightness) tft.setBrightness(brightness = cfg.brightness);
  dashboard::update(s, cfg);
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

  dashScreen = dashboard::create(openSettings);
  // Interim until the alarm screens land: any tap on the dashboard silences.
  lv_obj_add_flag(dashScreen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      dashScreen, [](lv_event_t*) { state::requestSilence(); }, LV_EVENT_CLICKED, nullptr);
  refresh();
  lv_screen_load(dashScreen);
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
