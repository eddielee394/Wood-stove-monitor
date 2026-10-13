#include "settings_screen.h"

#include <Arduino.h>

#include "net.h"
#include "theme.h"
#include "wifi_setup.h"

namespace ui::settings_screen {
namespace {

const int STEP_F = 25;

enum Tab { STOVE_TAB, FLUE_TAB, ALERTS_TAB, WIFI_TAB, TAB_COUNT };

struct Toggle {
  lv_obj_t* state;
  lv_obj_t* track;
  lv_obj_t* knob;
};

struct {
  lv_obj_t* screen;
  Tab tab = STOVE_TAB;
  lv_obj_t* tabBtn[TAB_COUNT];
  lv_obj_t* tabLabel[TAB_COUNT];
  lv_obj_t* wifi;
  lv_obj_t* limits;
  lv_obj_t* warnValue;
  lv_obj_t* critValue;
  lv_obj_t* note;
  lv_obj_t* alerts;
  lv_obj_t* reloadSub;
  Toggle reload, quiet;
  lv_obj_t* brightFill;
  lv_obj_t* brightValue;
  void (*onBack)();
} ui;

// Apply one change through the validated setter; a rejected value is simply
// not taken (the display keeps showing the stored setting).
void change(void (*mutate)(settings::Settings&)) {
  settings::Settings s = settings::get();
  mutate(s);
  char err[64];
  if (!settings::apply(s, err, sizeof(err))) Serial.printf("settings: %s\n", err);
}

int32_t& warnOf(settings::Settings& s) { return ui.tab == FLUE_TAB ? s.flueWarnF : s.stoveWarnF; }
int32_t& critOf(settings::Settings& s) { return ui.tab == FLUE_TAB ? s.flueCritF : s.stoveCritF; }

lv_obj_t* roundButton(lv_obj_t* parent, const lv_image_dsc_t* ic, lv_event_cb_t cb) {
  lv_obj_t* b = box(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, 56, 48);
  lv_obj_set_style_radius(b, 42, 0);
  lv_obj_set_style_bg_color(b, c(BTN_BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(BTN_BORDER), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, c(BTN_BORDER), 0);
  lv_obj_center(icon(b, ic, TEXT));
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

lv_obj_t* titled(lv_obj_t* parent, const char* title, const char* sub, lv_obj_t** subOut = nullptr) {
  lv_obj_t* col = box(parent);
  lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  label(col, &jakarta_20_xb, TEXT, title);
  lv_obj_t* s = label(col, &jakarta_14_sb, MUTED, sub);
  if (subOut) *subOut = s;
  return col;
}

lv_obj_t* settingRow(lv_obj_t* parent, int h, bool divider) {
  lv_obj_t* r = box(parent);
  lv_obj_set_size(r, 448, h);
  if (divider) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, c(DIVIDER), 0);
  }
  return r;
}

lv_obj_t* stepper(lv_obj_t* parent, lv_obj_t** valueOut, lv_event_cb_t down, lv_event_cb_t up) {
  lv_obj_t* r = row(parent, 0);
  roundButton(r, &icon_minus_24, down);
  *valueOut = label(r, &jakarta_20_xb, TEXT, "");
  lv_obj_set_width(*valueOut, 96);
  lv_obj_set_style_text_align(*valueOut, LV_TEXT_ALIGN_CENTER, 0);
  roundButton(r, &icon_plus_24, up);
  return r;
}

Toggle toggleRow(lv_obj_t* parent, const char* title, const char* sub, lv_event_cb_t cb,
                 lv_obj_t** subOut = nullptr) {
  lv_obj_t* r = settingRow(parent, 88, true);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(titled(r, title, sub, subOut), LV_ALIGN_LEFT_MID, 0, 0);
  Toggle t;
  lv_obj_t* right = row(r, 12);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
  t.state = label(right, &jakarta_20_xb, TEXT, "");
  t.track = box(right);
  lv_obj_set_size(t.track, 64, 36);
  lv_obj_set_style_radius(t.track, 42, 0);
  lv_obj_set_style_bg_opa(t.track, LV_OPA_COVER, 0);
  t.knob = box(t.track);
  lv_obj_set_size(t.knob, 28, 28);
  lv_obj_set_style_radius(t.knob, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(t.knob, c(TEXT), 0);
  lv_obj_set_style_bg_opa(t.knob, LV_OPA_COVER, 0);
  return t;
}

void setToggle(Toggle& t, bool on, const char* onText) {
  lv_label_set_text(t.state, on ? onText : "Off");
  lv_obj_set_style_text_color(t.state, c(on ? TEXT : MUTED), 0);
  lv_obj_set_style_bg_color(t.track, c(on ? ACCENT : DIM), 0);
  lv_obj_align(t.knob, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -4 : 4, 0);
}

void selectTab(Tab t) {
  ui.tab = t;
  for (int i = 0; i < TAB_COUNT; i++) {
    bool on = i == t;
    lv_obj_set_style_border_color(ui.tabBtn[i], c(on ? ACCENT : BG), 0);
    lv_obj_set_style_text_color(ui.tabLabel[i], c(on ? TEXT : MUTED), 0);
  }
  show(ui.limits, t == STOVE_TAB || t == FLUE_TAB);
  show(ui.alerts, t == ALERTS_TAB);
  show(ui.wifi, t == WIFI_TAB);
  // Opening the Wi-Fi tab scans straight away if there's nothing listed yet.
  if (t == WIFI_TAB && !net::scanning() && net::scanResults().empty()) net::requestScan();
  lv_label_set_text(ui.note, t == FLUE_TAB
                                 ? "Probe at the top of the chimney, where gas exits. Steps of 25°F."
                                 : "Probe on the flue pipe just above the stove collar. Steps of 25°F.");
  update(settings::get());
}

void buildHeader(lv_obj_t* scr) {
  lv_obj_t* head = box(scr);
  lv_obj_set_size(head, 480, 48);
  lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(head, 1, 0);
  lv_obj_set_style_border_color(head, c(BORDER), 0);

  lv_obj_t* back = box(head);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(back, 56, 48);
  lv_obj_center(icon(back, &icon_back_24, MUTED));
  lv_obj_add_event_cb(back, [](lv_event_t*) { if (ui.onBack) ui.onBack(); }, LV_EVENT_CLICKED, nullptr);

  const char* names[TAB_COUNT] = {"Stove", "Flue", "Alerts", "Wi-Fi"};
  const int tabW = (480 - 56) / TAB_COUNT;
  for (int i = 0; i < TAB_COUNT; i++) {
    lv_obj_t* b = box(head);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(b, 56 + i * tabW, 0);
    lv_obj_set_size(b, tabW, 47);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(b, 3, 0);
    ui.tabBtn[i] = b;
    ui.tabLabel[i] = label(b, &jakarta_20_xb, MUTED, names[i]);
    lv_obj_align(ui.tabLabel[i], LV_ALIGN_CENTER, 0, 2);
    lv_obj_add_event_cb(
        b, [](lv_event_t* e) { selectTab((Tab)(intptr_t)lv_event_get_user_data(e)); },
        LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

void buildLimits(lv_obj_t* scr) {
  ui.limits = box(scr);
  lv_obj_set_pos(ui.limits, 16, 48);
  lv_obj_set_size(ui.limits, 448, 272);
  lv_obj_set_flex_flow(ui.limits, LV_FLEX_FLOW_COLUMN);

  lv_obj_t* w = settingRow(ui.limits, 76, true);
  lv_obj_align(titled(w, "Warning", "Beeps · Quiet mode mutes"), LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* ws = stepper(
      w, &ui.warnValue,
      [](lv_event_t*) { change([](settings::Settings& s) { warnOf(s) -= STEP_F; }); },
      [](lv_event_t*) {
        change([](settings::Settings& s) {
          if (warnOf(s) + STEP_F < critOf(s)) warnOf(s) += STEP_F;
        });
      });
  lv_obj_align(ws, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* cr = settingRow(ui.limits, 76, true);
  lv_obj_align(titled(cr, "Critical", "Rapid beep · always sounds"), LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* cs = stepper(
      cr, &ui.critValue,
      [](lv_event_t*) {
        change([](settings::Settings& s) {
          if (critOf(s) - STEP_F > warnOf(s)) critOf(s) -= STEP_F;
        });
      },
      [](lv_event_t*) { change([](settings::Settings& s) { critOf(s) += STEP_F; }); });
  lv_obj_align(cs, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* noteRow = row(ui.limits, 8, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_top(noteRow, 24, 0);
  icon(noteRow, &icon_info_20, MUTED);
  ui.note = label(noteRow, &jakarta_14_sb, MUTED, "");
  lv_label_set_long_mode(ui.note, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(ui.note, 400);
}

void buildAlerts(lv_obj_t* scr) {
  ui.alerts = box(scr);
  lv_obj_set_pos(ui.alerts, 16, 48);
  lv_obj_set_size(ui.alerts, 448, 272);
  lv_obj_set_flex_flow(ui.alerts, LV_FLEX_FLOW_COLUMN);

  ui.reload = toggleRow(ui.alerts, "Reload alert", "", [](lv_event_t*) {
    change([](settings::Settings& s) { s.reloadOn = !s.reloadOn; });
  }, &ui.reloadSub);
  ui.quiet = toggleRow(ui.alerts, "Quiet mode", "Critical alarms always sound", [](lv_event_t*) {
    change([](settings::Settings& s) { s.quiet = !s.quiet; });
  });

  lv_obj_t* b = settingRow(ui.alerts, 96, false);
  lv_obj_t* t = label(b, &jakarta_20_xb, TEXT, "Brightness");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* r = row(b, 12);
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
  roundButton(r, &icon_minus_24, [](lv_event_t*) {
    change([](settings::Settings& s) { s.brightness = max(26, (int)s.brightness - 26); });
  });
  lv_obj_t* meter = box(r);
  lv_obj_set_size(meter, 104, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(meter, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(meter, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(meter, 4, 0);
  lv_obj_t* track = box(meter);
  lv_obj_set_size(track, 104, 12);
  lv_obj_set_style_radius(track, 42, 0);
  lv_obj_set_style_bg_color(track, c(BORDER), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_clip_corner(track, true, 0);
  ui.brightFill = box(track);
  lv_obj_set_height(ui.brightFill, 12);
  lv_obj_set_style_bg_color(ui.brightFill, c(ACCENT), 0);
  lv_obj_set_style_bg_opa(ui.brightFill, LV_OPA_COVER, 0);
  ui.brightValue = label(meter, &jakarta_14_xb, TEXT, "");
  roundButton(r, &icon_plus_24, [](lv_event_t*) {
    change([](settings::Settings& s) { s.brightness = min(255, (int)s.brightness + 26); });
  });
}

}  // namespace

lv_obj_t* create(void (*onBack)(), void (*openPassword)(), void (*closePassword)()) {
  ui.onBack = onBack;
  ui.screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(ui.screen);
  lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui.screen, c(BG), 0);
  lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);

  buildHeader(ui.screen);
  buildLimits(ui.screen);
  buildAlerts(ui.screen);
  ui.wifi = wifi_setup::buildTab(ui.screen, openPassword, closePassword);
  selectTab(STOVE_TAB);
  return ui.screen;
}

void update(const settings::Settings& cfg) {
  bool unitsC = cfg.unitsC;
  const char* u = unitsC ? "°C" : "°F";
  int warn = ui.tab == FLUE_TAB ? cfg.flueWarnF : cfg.stoveWarnF;
  int crit = ui.tab == FLUE_TAB ? cfg.flueCritF : cfg.stoveCritF;
  lv_label_set_text_fmt(ui.warnValue, "%d%s", (int)lroundf(toUnits(warn, unitsC)), u);
  lv_label_set_text_fmt(ui.critValue, "%d%s", (int)lroundf(toUnits(crit, unitsC)), u);

  lv_label_set_text_fmt(ui.reloadSub, "When the stove cools below %d%s",
                        (int)lroundf(toUnits(cfg.reloadF, unitsC)), u);
  setToggle(ui.reload, cfg.reloadOn, "On");
  setToggle(ui.quiet, cfg.quiet, "Quiet");

  int pct = (cfg.brightness * 100 + 127) / 255;
  lv_obj_set_width(ui.brightFill, 104 * pct / 100);
  lv_label_set_text_fmt(ui.brightValue, "%d%%", pct);
  if (ui.tab == WIFI_TAB) wifi_setup::updateTab();
}



}  // namespace ui::settings_screen
