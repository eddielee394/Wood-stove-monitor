#include "dashboard.h"

#include <Arduino.h>

#include "theme.h"

namespace ui::dashboard {
namespace {

const int W = 480;
const int BAR_H = 48, COL_H = 176, ZONE_Y = BAR_H + COL_H;
const int ZONE_PAD = 16, ZONE_W = W - 2 * ZONE_PAD;
const float SCALE_MIN = 100, SCALE_MAX = 900;  // burn zone bar span, deg F

struct Column {
  lv_obj_t* value;     // reading: big number + unit
  lv_obj_t* temp;
  lv_obj_t* unit;
  lv_obj_t* trend;     // rate row
  lv_obj_t* arrow[3];  // up / flat / down
  lv_obj_t* rate;
  lv_obj_t* word;
  lv_obj_t* fault;     // replaces value + trend when there is no reading
  lv_obj_t* faultTitle;
  lv_obj_t* faultNote;
};

struct {
  lv_obj_t* screen;
  lv_obj_t* quiet;
  lv_obj_t* freshLabel;
  lv_obj_t* faultChip;
  lv_obj_t* faultChipLabel;
  lv_obj_t* silencedChip;
  lv_obj_t* silencedLabel;
  lv_obj_t* faultStrip;      // artboard 3: probe fault with Silence button
  lv_obj_t* faultStripTitle;
  lv_obj_t* faultStripBtn;
  lv_obj_t* reloadBanner;    // artboard 2: "Time to reload"
  lv_obj_t* reloadText;
  Column col[thermo::COUNT];
  lv_obj_t* zoneStatus[5];  // ideal, chimney cool, too cool, hot, unavailable
  lv_obj_t* zoneBar;
  lv_obj_t* segLabel[3];
  lv_obj_t* seg[3];
  lv_obj_t* marker;
  void (*onSettings)();
} ui;

enum { Z_IDEAL, Z_CHIMNEY_COOL, Z_COOL, Z_HOT, Z_NONE };

void buildTopBar(lv_obj_t* scr) {
  lv_obj_t* bar = box(scr);
  lv_obj_set_size(bar, W, BAR_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, c(BORDER), 0);

  lv_obj_t* left = row(bar, 16);
  lv_obj_align(left, LV_ALIGN_LEFT_MID, 16, 0);

  // Wi-Fi status joins this row in Phase 5.
  ui.quiet = row(left, 6);
  icon(ui.quiet, &icon_quiet_20, MUTED);
  label(ui.quiet, &jakarta_14_sb, MUTED, "Quiet");

  lv_obj_t* fresh = row(left, 6);
  icon(fresh, &icon_fresh_20, MUTED);
  ui.freshLabel = label(fresh, &jakarta_14_sb, MUTED, "--");

  ui.faultChip = row(left, 6);
  icon(ui.faultChip, &icon_warning_20, AMBER);
  ui.faultChipLabel = label(ui.faultChip, &jakarta_14_xb, AMBER, "");

  ui.silencedChip = row(left, 6);
  icon(ui.silencedChip, &icon_silence_22, AMBER);
  ui.silencedLabel = label(ui.silencedChip, &jakarta_14_xb, AMBER, "");

  // Settings button (History arrives in Phase 7).
  lv_obj_t* btn = box(bar);
  lv_obj_set_size(btn, 56, BAR_H);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* gear = icon(btn, &icon_settings_24, MUTED);
  lv_obj_center(gear);
  lv_obj_add_event_cb(
      btn, [](lv_event_t*) { if (ui.onSettings) ui.onSettings(); }, LV_EVENT_CLICKED, nullptr);
}

void buildColumn(lv_obj_t* scr, int i) {
  Column& k = ui.col[i];
  lv_obj_t* col = box(scr);
  lv_obj_set_pos(col, i * (W / 2), BAR_H);
  lv_obj_set_size(col, W / 2, COL_H);
  if (i == 0) {
    lv_obj_set_style_border_side(col, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(col, 1, 0);
    lv_obj_set_style_border_color(col, c(BORDER), 0);
  }

  lv_obj_t* name = row(col, 8, LV_FLEX_ALIGN_END);
  lv_obj_set_pos(name, 16, 12);
  lv_obj_t* n = label(name, &jakarta_20_xb, MUTED, i == thermo::STOVE ? "STOVE" : "FLUE");
  lv_obj_set_style_text_letter_space(n, 1, 0);
  label(name, &jakarta_14_sb, SUBTLE, i == thermo::STOVE ? "collar pipe" : "chimney exit");

  // 96px reading; digit tops sit ~13px into the design's 96px line box.
  k.value = row(col, 4, LV_FLEX_ALIGN_START);
  lv_obj_set_pos(k.value, 16, 41);
  k.temp = label(k.value, &jakarta_96_xb, TEXT, "--");
  lv_obj_set_style_text_letter_space(k.temp, -3, 0);
  lv_obj_set_style_margin_top(k.temp, 13, 0);
  k.unit = label(k.value, &jakarta_20_xb, MUTED, "°F");
  lv_obj_set_style_margin_top(k.unit, 12, 0);

  k.trend = row(col, 8);
  lv_obj_set_pos(k.trend, 16, 140);
  const lv_image_dsc_t* arrows[3] = {&icon_trend_up_24, &icon_trend_flat_24, &icon_trend_down_24};
  for (int a = 0; a < 3; a++) k.arrow[a] = icon(k.trend, arrows[a], TEXT);
  k.rate = label(k.trend, &jakarta_20_xb, TEXT, "");
  k.word = label(k.trend, &jakarta_20_sb, MUTED, "");

  // No-reading state (artboard 3)
  k.fault = box(col);
  lv_obj_set_size(k.fault, W / 2 - 32, 130);
  lv_obj_set_pos(k.fault, 16, 41);
  lv_obj_t* dashed = box(k.fault);
  lv_obj_set_size(dashed, 72, 72);
  lv_obj_set_pos(dashed, 0, 12);
  lv_obj_set_style_border_width(dashed, 2, 0);
  lv_obj_set_style_border_color(dashed, c(DASH), 0);
  lv_obj_set_style_radius(dashed, 4, 0);
  lv_obj_center(icon(dashed, &icon_no_probe_32, SUBTLE));
  lv_obj_t* words = box(k.fault);
  lv_obj_set_size(words, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(words, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(words, 2, 0);
  lv_obj_set_pos(words, 84, 30);
  k.faultTitle = label(words, &jakarta_20_xb, AMBER, "No signal");
  label(words, &jakarta_14_sb, MUTED, "No reading shown");
  k.faultNote = label(k.fault, &jakarta_14_sb, MUTED, "");
  lv_obj_set_pos(k.faultNote, 0, 102);
}

void buildZone(lv_obj_t* scr) {
  lv_obj_t* zone = box(scr);
  lv_obj_set_pos(zone, 0, ZONE_Y);
  lv_obj_set_size(zone, W, 320 - ZONE_Y);
  lv_obj_set_style_border_side(zone, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(zone, 1, 0);
  lv_obj_set_style_border_color(zone, c(BORDER), 0);

  lv_obj_t* title = label(zone, &jakarta_14_xb, MUTED, "BURN ZONE");
  lv_obj_set_style_text_letter_space(title, 1, 0);
  lv_obj_set_pos(title, ZONE_PAD, 16);

  struct { const lv_image_dsc_t* icon; uint32_t colour; const char* text; const char* sub; } st[5] = {
      {&icon_ideal_22, GREEN, "Ideal burn", nullptr},
      {&icon_warning_22, AMBER, "Chimney too cool", "· creosote risk"},
      {&icon_too_cool_22, COOL_TEXT, "Too cool", nullptr},
      {&icon_warning_22, AMBER, "Running hot", nullptr},
      {nullptr, MUTED, "Unavailable", "· needs the stove probe"},
  };
  for (int i = 0; i < 5; i++) {
    lv_obj_t* r = row(zone, 8);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -ZONE_PAD, 13);
    if (st[i].icon) icon(r, st[i].icon, st[i].colour);
    label(r, &jakarta_20_xb, st[i].colour, st[i].text);
    if (st[i].sub) label(r, &jakarta_14_sb, st[i].colour, st[i].sub);
    ui.zoneStatus[i] = r;
  }

  ui.zoneBar = box(zone);
  lv_obj_set_pos(ui.zoneBar, ZONE_PAD, 56);
  lv_obj_set_size(ui.zoneBar, ZONE_W, 28);
  lv_obj_set_style_radius(ui.zoneBar, 4, 0);
  lv_obj_set_style_clip_corner(ui.zoneBar, true, 0);
  const uint32_t bg[3] = {ZONE_COOL_BG, ZONE_IDEAL_BG, ZONE_HOT_BG};
  const uint32_t fg[3] = {ZONE_COOL_FG, ZONE_IDEAL_FG, ZONE_HOT_FG};
  for (int i = 0; i < 3; i++) {
    ui.seg[i] = box(ui.zoneBar);
    lv_obj_set_height(ui.seg[i], 28);
    lv_obj_set_style_bg_color(ui.seg[i], c(bg[i]), 0);
    lv_obj_set_style_bg_opa(ui.seg[i], LV_OPA_COVER, 0);
    if (i == 1) {  // dark gaps either side of the ideal segment
      lv_obj_set_style_border_side(ui.seg[i], (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
      lv_obj_set_style_border_width(ui.seg[i], 2, 0);
      lv_obj_set_style_border_color(ui.seg[i], c(BG), 0);
    }
    ui.segLabel[i] = label(ui.seg[i], &jakarta_14_xb, fg[i], "");
    lv_obj_center(ui.segLabel[i]);
  }

  // Marker: a downward triangle over a 4px stem with dark edges.
  ui.marker = box(zone);
  lv_obj_set_size(ui.marker, 20, 40);
  static lv_point_precise_t tri[] = {{0, 0}, {20, 0}, {10, 11}, {0, 0}};
  lv_obj_t* head = lv_line_create(ui.marker);
  lv_line_set_points(head, tri, 4);
  lv_obj_set_style_line_width(head, 2, 0);
  lv_obj_set_style_line_color(head, c(TEXT), 0);
  for (int y = 1; y < 11; y++) {  // fill the triangle
    lv_obj_t* f = box(ui.marker);
    int half = 10 - y * 10 / 11;
    lv_obj_set_pos(f, 10 - half, y);
    lv_obj_set_size(f, half * 2, 1);
    lv_obj_set_style_bg_color(f, c(TEXT), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
  }
  lv_obj_t* stem = box(ui.marker);
  lv_obj_set_pos(stem, 6, 11);
  lv_obj_set_size(stem, 8, 29);
  lv_obj_set_style_bg_color(stem, c(TEXT), 0);
  lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(stem, 2, 0);
  lv_obj_set_style_border_side(stem, (lv_border_side_t)(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
  lv_obj_set_style_border_color(stem, c(BG), 0);
}

lv_obj_t* pillButton(lv_obj_t* parent, int w, int h, uint32_t bg, uint32_t fg, uint32_t border,
                     const lv_image_dsc_t* ic, const char* text, lv_obj_t** labelOut) {
  lv_obj_t* b = box(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 42, 0);
  lv_obj_set_style_bg_color(b, c(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  if (border) {
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, c(border), 0);
  }
  lv_obj_t* r = row(b, 8);
  lv_obj_center(r);
  if (ic) icon(r, ic, fg);
  lv_obj_t* l = label(r, &jakarta_20_xb, fg, text);
  if (labelOut) *labelOut = l;
  lv_obj_add_event_cb(
      b, [](lv_event_t*) { state::requestSilence(); }, LV_EVENT_CLICKED, nullptr);
  return b;
}

// Bottom-panel overlays that replace the burn zone while they apply.
void buildOverlays(lv_obj_t* scr) {
  ui.faultStrip = box(scr);
  lv_obj_set_pos(ui.faultStrip, 0, ZONE_Y);
  lv_obj_set_size(ui.faultStrip, W, 320 - ZONE_Y);
  lv_obj_set_style_bg_color(ui.faultStrip, c(BG), 0);
  lv_obj_set_style_bg_opa(ui.faultStrip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(ui.faultStrip, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(ui.faultStrip, 1, 0);
  lv_obj_set_style_border_color(ui.faultStrip, c(BORDER), 0);
  lv_obj_t* words = box(ui.faultStrip);
  lv_obj_set_size(words, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(words, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(words, 2, 0);
  lv_obj_align(words, LV_ALIGN_LEFT_MID, ZONE_PAD, 0);
  lv_obj_t* t = label(words, &jakarta_14_xb, MUTED, "PROBE FAULT");
  lv_obj_set_style_text_letter_space(t, 1, 0);
  ui.faultStripTitle = label(words, &jakarta_20_xb, AMBER, "");
  label(words, &jakarta_14_sb, SUBTLE, "No temperature shown");
  lv_obj_t* fb = pillButton(ui.faultStrip, 200, 56, 0x231A0A, AMBER, AMBER, &icon_silence_22,
                            "Silence", &ui.faultStripBtn);
  lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -ZONE_PAD, 0);

  ui.reloadBanner = box(scr);
  lv_obj_set_pos(ui.reloadBanner, 0, 320 - 96);
  lv_obj_set_size(ui.reloadBanner, W, 96);
  lv_obj_set_style_bg_color(ui.reloadBanner, c(RELOAD_BG), 0);
  lv_obj_set_style_bg_opa(ui.reloadBanner, LV_OPA_COVER, 0);
  lv_obj_t* ri = icon(ui.reloadBanner, &icon_reload_32, RELOAD_FG);
  lv_obj_align(ri, LV_ALIGN_LEFT_MID, ZONE_PAD, 0);
  lv_obj_t* rw = box(ui.reloadBanner);
  lv_obj_set_size(rw, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(rw, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(rw, 2, 0);
  lv_obj_align(rw, LV_ALIGN_LEFT_MID, ZONE_PAD + 44, 0);
  label(rw, &jakarta_20_xb, RELOAD_FG, "Time to reload");
  ui.reloadText = label(rw, &jakarta_14_sb, RELOAD_FG, "");
  lv_obj_t* ok = pillButton(ui.reloadBanner, 104, 56, RELOAD_FG, RELOAD_BTN_FG, 0, nullptr, "OK",
                            nullptr);
  lv_obj_align(ok, LV_ALIGN_RIGHT_MID, -ZONE_PAD, 0);
}

int scaleX(float f) {
  if (f < SCALE_MIN) f = SCALE_MIN;
  if (f > SCALE_MAX) f = SCALE_MAX;
  return (int)((f - SCALE_MIN) / (SCALE_MAX - SCALE_MIN) * ZONE_W + 0.5f);
}

void updateZone(const state::Snapshot& s, const settings::Settings& cfg) {
  bool unitsC = cfg.unitsC;
  const state::Channel& st = s.ch[thermo::STOVE];
  const state::Channel& fl = s.ch[thermo::FLUE];

  // Segment widths follow the configured ideal band.
  int x1 = scaleX(cfg.idealLoF), x2 = scaleX(cfg.idealHiF);
  lv_obj_set_pos(ui.seg[0], 0, 0);
  lv_obj_set_width(ui.seg[0], x1);
  lv_obj_set_pos(ui.seg[1], x1, 0);
  lv_obj_set_width(ui.seg[1], x2 - x1);
  lv_obj_set_pos(ui.seg[2], x2, 0);
  lv_obj_set_width(ui.seg[2], ZONE_W - x2);
  int lo = (int)lroundf(toUnits(cfg.idealLoF, unitsC));
  int hi = (int)lroundf(toUnits(cfg.idealHiF, unitsC));
  lv_label_set_text_fmt(ui.segLabel[0], "COOL <%d", lo);
  lv_label_set_text_fmt(ui.segLabel[1], "IDEAL %d–%d", lo, hi);
  lv_label_set_text_fmt(ui.segLabel[2], "HOT %d+", hi);

  int which = Z_NONE;
  if (st.valid) {
    if (s.alarms.ideal) which = Z_IDEAL;
    else if (st.tempF < cfg.idealLoF) which = Z_COOL;
    else if (st.tempF > cfg.idealHiF) which = Z_HOT;
    else if (fl.valid && fl.tempF < cfg.idealExitF) which = Z_CHIMNEY_COOL;
    else which = Z_IDEAL;  // inside the deadband while the flag settles
  }
  for (int i = 0; i < 5; i++) show(ui.zoneStatus[i], i == which);

  show(ui.marker, st.valid);
  if (st.valid) lv_obj_set_pos(ui.marker, ZONE_PAD + scaleX(st.tempF) - 10, 44);
}

void updateColumn(int i, const state::Channel& ch, bool unitsC) {
  Column& k = ui.col[i];
  bool ok = ch.valid;
  show(k.value, ok);
  show(k.trend, ok);
  show(k.fault, !ok);
  if (!ok) {
    lv_label_set_text(k.faultTitle, ch.present ? "No signal" : "No sensor");
    lv_label_set_text(k.faultNote, ch.present ? "Check probe at the sensor box"
                                              : "Check the sensor box cable");
    return;
  }

  lv_label_set_text_fmt(k.temp, "%d", (int)lroundf(toUnits(ch.tempF, unitsC)));
  lv_label_set_text(k.unit, unitsC ? "°C" : "°F");

  if (isnan(ch.rateFpm)) {
    for (auto* a : k.arrow) show(a, false);
    lv_label_set_text(k.rate, "--");
    lv_label_set_text(k.word, "Measuring");
    return;
  }
  int r = (int)lroundf(rateToUnits(ch.rateFpm, unitsC));
  show(k.arrow[0], r > 3);
  show(k.arrow[1], r >= -3 && r <= 3);
  show(k.arrow[2], r < -3);
  lv_label_set_text_fmt(k.rate, "%s%d°%c/min", r > 0 ? "+" : r < 0 ? "−" : "±", abs(r),
                        unitsC ? 'C' : 'F');
  lv_label_set_text(k.word, r > 3 ? "Rising" : r < -3 ? "Falling" : "Steady");
}

}  // namespace

lv_obj_t* create(void (*onSettings)()) {
  ui.onSettings = onSettings;
  ui.screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(ui.screen);
  lv_obj_set_style_bg_color(ui.screen, c(BG), 0);
  lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(ui.screen);
  for (int i = 0; i < thermo::COUNT; i++) buildColumn(ui.screen, i);
  buildZone(ui.screen);
  buildOverlays(ui.screen);
  return ui.screen;
}

void update(const state::Snapshot& s, const settings::Settings& cfg) {
  show(ui.quiet, cfg.quiet);

  uint32_t age = s.sensorMs ? (millis() - s.sensorMs) / 1000 : 0;
  lv_label_set_text_fmt(ui.freshLabel, s.sensorMs ? "%lus ago" : "starting", (unsigned long)age);
  lv_obj_set_style_text_color(ui.freshLabel, c(age > 3 ? AMBER : MUTED), 0);

  int faults = 0, which = 0;
  for (int i = 0; i < thermo::COUNT; i++) {
    if (s.alarms.fault[i]) faults++, which = i;
  }
  show(ui.faultChip, faults > 0);
  if (faults == 1) {
    lv_label_set_text(ui.faultChipLabel, which == thermo::STOVE ? "Stove probe fault" : "Flue probe fault");
  } else if (faults > 1) {
    lv_label_set_text(ui.faultChipLabel, "Probe faults");
  }

  const state::Alarms& a = s.alarms;
  show(ui.silencedChip, a.silenced);
  if (a.silenced) {
    lv_label_set_text_fmt(ui.silencedLabel, "Silenced %u:%02u", a.silenceLeftS / 60,
                          a.silenceLeftS % 60);
  }

  for (int i = 0; i < thermo::COUNT; i++) updateColumn(i, s.ch[i], cfg.unitsC);
  updateZone(s, cfg);

  // The fault strip outranks the reload banner (same panel). Bench mode
  // hides it; the column and top-bar chip still show the fault.
  bool faultStrip = faults > 0 && !a.silenced && !state::benchMuteFaults();
  show(ui.faultStrip, faultStrip);
  if (faultStrip) {
    lv_label_set_text(ui.faultStripTitle, faults > 1 ? "Both probes"
                                          : which == thermo::STOVE ? "Stove probe" : "Flue probe");
    lv_label_set_text_fmt(ui.faultStripBtn, "Silence %d min", (int)cfg.silenceMin);
  }
  bool reload = a.reload && !a.silenced && !faultStrip;
  show(ui.reloadBanner, reload);
  if (reload && s.ch[thermo::STOVE].valid) {
    float t = s.ch[thermo::STOVE].tempF;
    float r = s.ch[thermo::STOVE].rateFpm;
    lv_label_set_text_fmt(ui.reloadText, "Stove is down to %d°%c%s.",
                          (int)lroundf(toUnits(t, cfg.unitsC)), cfg.unitsC ? 'C' : 'F',
                          (!isnan(r) && r < -1) ? " and falling" : "");
  }
}

}  // namespace ui::dashboard
