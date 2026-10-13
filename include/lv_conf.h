// LVGL 9 configuration for the stove monitor. Anything not set here takes
// LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the C library allocator: on this board large blocks land in PSRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 33  // ~30 fps is plenty for this UI
#define LV_DPI_DEF 160

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

// Fonts come from src/ui/assets (Plus Jakarta Sans); no built-in fonts.
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(jakarta_14_sb)
#define LV_FONT_DEFAULT &jakarta_14_sb

#define LV_USE_THEME_DEFAULT 0  // every style is set explicitly from the design

#endif  // LV_CONF_H
