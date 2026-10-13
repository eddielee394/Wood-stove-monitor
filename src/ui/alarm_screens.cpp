#include "alarm_screens.h"

#include <Arduino.h>

#include "theme.h"

namespace ui::alarm_screens {
namespace {

struct Look {
  uint32_t bg, fg;            // body
  uint32_t headBg, headFg;    // header strip
  uint32_t btnBg, btnFg;      // silence button
  const lv_image_dsc_t* headIcon;
  int headH;
};

const Look LOOKS[3] = {
    {WARN_BG, WARN_FG, WARN_FG, WARN_BG, WARN_FG, WARN_BG, &icon_warning_24, 48},
    {CRIT_BG, CRIT_FG, CRIT_HEAD, CRIT_FG, CRIT_FG, CRIT_BG, &icon_critical_24, 48},
    {CRIT_BG, CRIT_FG, CRIT_FG, CRIT_BG, CRIT_FG, CRIT_BG, &icon_flame_28, 56},
};

struct Screen {
  lv_obj_t* scr;
  lv_obj_t* title;
  lv_obj_t* beepIcon[2];  // beeping / quiet
  lv_obj_t* beepLabel;
  lv_obj_t* probeLabel;
  lv_obj_t* temp;
  lv_obj_t* unit;
  lv_obj_t* limit;        // "Warning 850°F" (not on chimney fire)
  lv_obj_t* rateIcon;
  lv_obj_t* rate;
  lv_obj_t* otherName;    // right column: the other probe
  lv_obj_t* otherTemp;
  lv_obj_t* otherState;
  lv_obj_t* btnLabel;
  lv_obj_t* note;
} screens[3];

lv_obj_t* buildSilenceButton(lv_obj_t* parent, const Look& L, int w, lv_obj_t** labelOut) {
  lv_obj_t* btn = box(parent);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(btn, w, 64);
  lv_obj_set_style_radius(btn, 42, 0);
  lv_obj_set_style_bg_color(btn, c(L.btnBg), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);
  lv_obj_t* r = row(btn, 10);
  lv_obj_center(r);
  icon(r, &icon_silence_24, L.btnFg);
  *labelOut = label(r, &jakarta_20_xb, L.btnFg, "Silence");
  lv_obj_add_event_cb(
      btn, [](lv_event_t*) { state::requestSilence(); }, LV_EVENT_CLICKED, nullptr);
  return btn;
}

void build(int k) {
  const Look& L = LOOKS[k];
  Screen& S = screens[k];
  bool chimney = k == (int)Kind::ChimneyFire;

  S.scr = lv_obj_create(nullptr);
  lv_obj_remove_style_all(S.scr);
  lv_obj_remove_flag(S.scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(S.scr, c(L.bg), 0);
  lv_obj_set_style_bg_opa(S.scr, LV_OPA_COVER, 0);

  // Header strip
  lv_obj_t* head = box(S.scr);
  lv_obj_set_size(head, 480, L.headH);
  lv_obj_set_style_bg_color(head, c(L.headBg), 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
  lv_obj_t* t = row(head, 10);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
  icon(t, L.headIcon, L.headFg);
  S.title = label(t, &jakarta_20_xb, L.headFg, chimney ? "POSSIBLE CHIMNEY FIRE" : "");
  lv_obj_set_style_text_letter_space(S.title, 1, 0);
  lv_obj_t* beep = row(head, 6);
  lv_obj_align(beep, LV_ALIGN_RIGHT_MID, -16, 0);
  S.beepIcon[0] = icon(beep, &icon_beeping_20, L.headFg);
  S.beepIcon[1] = icon(beep, &icon_quiet_20, L.headFg);
  S.beepLabel = label(beep, k == 0 ? &jakarta_14_sb : &jakarta_14_xb, L.headFg, "");

  // Reading
  int top = L.headH + 12;
  S.probeLabel = label(S.scr, chimney ? &jakarta_14_xb : &jakarta_14_sb, L.fg, "");
  if (chimney) lv_obj_set_style_text_letter_space(S.probeLabel, 1, 0);
  lv_obj_set_pos(S.probeLabel, 16, top);
  lv_obj_t* val = row(S.scr, 4, LV_FLEX_ALIGN_START);
  lv_obj_set_pos(val, 16, top + 20);
  S.temp = label(val, &jakarta_96_xb, L.fg, "--");
  lv_obj_set_style_text_letter_space(S.temp, -3, 0);
  lv_obj_set_style_margin_top(S.temp, 13, 0);
  S.unit = label(val, &jakarta_20_xb, L.fg, "°F");
  lv_obj_set_style_margin_top(S.unit, 12, 0);

  lv_obj_t* info = row(S.scr, 16);
  lv_obj_set_pos(info, 16, top + 20 + 96 + 6);
  S.limit = chimney ? nullptr : label(info, &jakarta_20_xb, L.fg, "");
  lv_obj_t* rr = row(info, 6);
  S.rateIcon = icon(rr, chimney ? &icon_rising_24 : &icon_trend_up_24, L.fg);
  S.rate = label(rr, &jakarta_20_xb, L.fg, "");

  // Right column: the other probe, or the chimney-fire action steps
  if (chimney) {
    lv_obj_t* steps = box(S.scr);
    lv_obj_set_size(steps, 200, LV_SIZE_CONTENT);
    lv_obj_set_pos(steps, 264, top + 22);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(steps, 10, 0);
    const char* text[3] = {"Close stove door and air control", "Get everyone out", "Call 911"};
    for (int i = 0; i < 3; i++) {
      lv_obj_t* r = row(steps, 8, LV_FLEX_ALIGN_START);
      lv_obj_t* num = box(r);
      lv_obj_set_size(num, 20, 20);
      lv_obj_set_style_radius(num, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(num, c(CRIT_FG), 0);
      lv_obj_set_style_bg_opa(num, LV_OPA_COVER, 0);
      lv_obj_t* n = label(num, &jakarta_14_xb, CRIT_BG, "");
      lv_label_set_text_fmt(n, "%d", i + 1);
      lv_obj_center(n);
      lv_obj_t* l = label(r, &jakarta_14_xb, L.fg, text[i]);
      lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
      lv_obj_set_width(l, 172);
    }
  } else {
    lv_obj_t* other = box(S.scr);
    lv_obj_set_size(other, 128, 90);
    lv_obj_set_pos(other, 480 - 16 - 128, top);
    lv_obj_set_style_border_side(other, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(other, 2, 0);
    lv_obj_set_style_border_color(other, c(L.fg), 0);
    lv_obj_set_style_pad_left(other, 16, 0);
    lv_obj_set_style_pad_top(other, 22, 0);
    lv_obj_set_flex_flow(other, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(other, 4, 0);
    S.otherName = label(other, &jakarta_14_xb, L.fg, "");
    lv_obj_set_style_text_letter_space(S.otherName, 1, 0);
    S.otherTemp = label(other, &jakarta_20_xb, L.fg, "");
    S.otherState = label(other, &jakarta_14_sb, L.fg, "");
  }

  // Silence button and note
  lv_obj_t* btn = buildSilenceButton(S.scr, L, chimney ? 232 : 290, &S.btnLabel);
  lv_obj_set_pos(btn, 16, 320 - 16 - 64);
  S.note = label(S.scr, &jakarta_14_sb, L.fg, "");
  lv_label_set_long_mode(S.note, LV_LABEL_LONG_MODE_WRAP);
  int noteX = 16 + (chimney ? 232 : 290) + 16;
  lv_obj_set_width(S.note, 480 - noteX - 16);
  lv_obj_set_pos(S.note, noteX, 320 - 16 - 44);
}

const char* probeName(int i) { return i == thermo::STOVE ? "STOVE" : "FLUE"; }
const char* probeWhere(int i) { return i == thermo::STOVE ? "collar pipe" : "chimney exit"; }

}  // namespace

void create() {
  for (int k = 0; k < 3; k++) build(k);
}

lv_obj_t* screen(Kind k) { return screens[(int)k].scr; }

void update(Kind k, int probe, const state::Snapshot& s, const settings::Settings& cfg) {
  Screen& S = screens[(int)k];
  bool unitsC = cfg.unitsC;
  bool chimney = k == Kind::ChimneyFire;
  if (chimney) probe = thermo::FLUE;
  const state::Channel& ch = s.ch[probe];
  char deg[4] = {'\xC2', '\xB0', unitsC ? 'C' : 'F', 0};  // "°F" / "°C"

  bool muted = !s.alarms.audible;
  show(S.beepIcon[0], !muted);
  show(S.beepIcon[1], muted);
  if (k == Kind::Warning) lv_label_set_text(S.beepLabel, muted ? "Quiet" : "Beeping");
  else lv_label_set_text(S.beepLabel, muted ? "Quiet" : "Rapid beep");

  if (!chimney) {
    lv_label_set_text_fmt(S.title, "%s %s", probeName(probe),
                          k == Kind::Warning ? "WARNING" : "CRITICAL");
  }
  lv_label_set_text_fmt(S.probeLabel, "%s · %s", probeName(probe), probeWhere(probe));

  if (ch.valid) lv_label_set_text_fmt(S.temp, "%d", (int)lroundf(toUnits(ch.tempF, unitsC)));
  else lv_label_set_text(S.temp, "--");
  lv_label_set_text(S.unit, deg);

  if (S.limit) {
    int warn = probe == thermo::STOVE ? cfg.stoveWarnF : cfg.flueWarnF;
    int crit = probe == thermo::STOVE ? cfg.stoveCritF : cfg.flueCritF;
    int lim = k == Kind::Warning ? warn : crit;
    lv_label_set_text_fmt(S.limit, "%s %d%s", k == Kind::Warning ? "Warning" : "Critical",
                          (int)lroundf(toUnits(lim, unitsC)), deg);
  }
  if (isnan(ch.rateFpm)) {
    show(S.rateIcon, false);
    lv_label_set_text(S.rate, "");
  } else {
    int r = (int)lroundf(rateToUnits(ch.rateFpm, unitsC));
    show(S.rateIcon, r > 0);
    lv_label_set_text_fmt(S.rate, "%s%d%s/min", r > 0 ? "+" : r < 0 ? "−" : "±", abs(r), deg);
  }

  if (!chimney) {
    int o = probe == thermo::STOVE ? thermo::FLUE : thermo::STOVE;
    const state::Channel& oc = s.ch[o];
    lv_label_set_text(S.otherName, probeName(o));
    if (oc.valid) {
      lv_label_set_text_fmt(S.otherTemp, "%d%s", (int)lroundf(toUnits(oc.tempF, unitsC)), deg);
    } else {
      lv_label_set_text(S.otherTemp, "--");
    }
    const char* st = !oc.valid ? "No signal"
                     : s.alarms.temp[o] == state::Level::Critical ? "Critical"
                     : s.alarms.temp[o] == state::Level::Warning ? "Warning"
                     : "Below warning";
    lv_label_set_text(S.otherState, st);
  }

  int mins = k == Kind::Warning ? cfg.silenceMin : cfg.silenceCritMin;
  lv_label_set_text_fmt(S.btnLabel, "Silence %d min", mins);
  if (k != Kind::Critical) {
    lv_label_set_text_fmt(S.note, "Alarm returns while the %s keeps climbing.",
                          probe == thermo::STOVE ? "stove" : "flue");
  } else {
    lv_label_set_text_fmt(S.note, "Alarm returns early if the %s keeps climbing.",
                          probe == thermo::STOVE ? "stove" : "flue");
  }
}

}  // namespace ui::alarm_screens
