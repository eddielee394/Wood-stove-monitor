// Colours, fonts and small widget helpers, taken from the "Wood Stove
// Monitor" design canvas (480x320, Plus Jakarta Sans 600/800).
#pragma once

#include <lvgl.h>

#include "assets/fonts.h"
#include "assets/icons.h"

namespace ui {

// Dark dashboard
constexpr uint32_t BG = 0x0E141B;
constexpr uint32_t TEXT = 0xE8EEF4;
constexpr uint32_t MUTED = 0x9FB2C8;
constexpr uint32_t SUBTLE = 0x7F93AA;
constexpr uint32_t BORDER = 0x243040;
constexpr uint32_t DIVIDER = 0x1C2733;
constexpr uint32_t GREEN = 0x4FD18B;
constexpr uint32_t COOL_TEXT = 0x8DBDF0;
constexpr uint32_t AMBER = 0xF5B547;
constexpr uint32_t ACCENT = 0x1271EB;
constexpr uint32_t BTN_BG = 0x17212C;
constexpr uint32_t BTN_BORDER = 0x2E3D4F;
constexpr uint32_t DIM = 0x3A4757;
constexpr uint32_t DASH = 0x5C6B7D;

// Burn zone bar
constexpr uint32_t ZONE_COOL_BG = 0x26405E, ZONE_COOL_FG = 0xCFDDEC;
constexpr uint32_t ZONE_IDEAL_BG = 0x1C7347, ZONE_IDEAL_FG = 0xE6F6EC;
constexpr uint32_t ZONE_HOT_BG = 0x6B4A14, ZONE_HOT_FG = 0xF8E7C8;

// Alarm screens
constexpr uint32_t WARN_BG = 0xF29A1F, WARN_FG = 0x1A1205;
constexpr uint32_t CRIT_BG = 0xB0102C, CRIT_HEAD = 0x3A0610, CRIT_FG = 0xFFF1F3;
constexpr uint32_t RELOAD_BG = 0x1271EB, RELOAD_FG = 0xF2F7FF, RELOAD_BTN_FG = 0x0B4FA8;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// A bare container: no theme styles, no scrolling, transparent.
inline lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

inline lv_obj_t* row(lv_obj_t* parent, int gap, lv_flex_align_t cross = LV_FLEX_ALIGN_CENTER) {
  lv_obj_t* o = box(parent);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, cross, cross);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

inline lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t colour,
                       const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(colour), 0);
  lv_label_set_text(l, text);
  return l;
}

inline lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* dsc, uint32_t colour) {
  lv_obj_t* i = lv_image_create(parent);
  lv_image_set_src(i, dsc);
  lv_obj_set_style_image_recolor(i, c(colour), 0);
  lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
  return i;
}

inline void show(lv_obj_t* o, bool visible) {
  if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Display conversion; settings and state are always deg F.
inline float toUnits(float f, bool unitsC) { return unitsC ? (f - 32.0f) * 5.0f / 9.0f : f; }
inline float rateToUnits(float fpm, bool unitsC) { return unitsC ? fpm * 5.0f / 9.0f : fpm; }

}  // namespace ui
