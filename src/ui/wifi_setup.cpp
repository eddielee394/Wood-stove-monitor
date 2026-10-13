#include "wifi_setup.h"

#include <Arduino.h>

#include "net.h"
#include "theme.h"

namespace ui::wifi_setup {
namespace {

struct {
  lv_obj_t* tab;
  lv_obj_t* title;
  lv_obj_t* detail;
  lv_obj_t* forgetBtn;
  lv_obj_t* scanLabel;
  lv_obj_t* list;
  size_t listSig = 0;   // rebuild the list only when results change
  bool wasScanning = false;

  lv_obj_t* pwScreen;
  lv_obj_t* pwTitle;
  lv_obj_t* textarea;
  lv_obj_t* showLabel;
  String joining;       // SSID the password screen is for
  void (*openPassword)();
  void (*closePassword)();
} ui;

lv_obj_t* pill(lv_obj_t* parent, int w, const char* text, lv_obj_t** labelOut, lv_event_cb_t cb) {
  lv_obj_t* b = box(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, w, 44);
  lv_obj_set_style_radius(b, 42, 0);
  lv_obj_set_style_bg_color(b, c(BTN_BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, c(BTN_BORDER), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, c(BTN_BORDER), 0);
  lv_obj_t* l = label(b, &jakarta_20_xb, TEXT, text);
  lv_obj_center(l);
  if (labelOut) *labelOut = l;
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

const char* strength(int8_t rssi) {
  return rssi >= -55 ? "Strong" : rssi >= -67 ? "Good" : rssi >= -78 ? "Fair" : "Weak";
}

void startJoin(const String& ssid, bool secured) {
  if (!secured) {
    net::connect(ssid.c_str(), "");
    return;
  }
  ui.joining = ssid;
  lv_label_set_text_fmt(ui.pwTitle, "Join %s", ssid.c_str());
  lv_textarea_set_text(ui.textarea, "");
  lv_textarea_set_password_mode(ui.textarea, true);
  lv_label_set_text(ui.showLabel, "Show");
  if (ui.openPassword) ui.openPassword();
}

void rebuildList(const std::vector<net::Network>& nets) {
  lv_obj_clean(ui.list);
  static std::vector<net::Network> shown;  // event callbacks index into this
  shown = nets;
  for (size_t i = 0; i < shown.size(); i++) {
    lv_obj_t* r = box(ui.list);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(r, 448, 48);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, c(DIVIDER), 0);
    lv_obj_set_style_bg_color(r, c(BTN_BG), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t* name = label(r, &jakarta_20_sb, TEXT, shown[i].ssid.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, 290);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* info = label(r, &jakarta_14_sb, MUTED, "");
    lv_label_set_text_fmt(info, "%s%s", strength(shown[i].rssi), shown[i].secured ? " · secured" : " · open");
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(
        r,
        [](lv_event_t* e) {
          size_t idx = (size_t)(intptr_t)lv_event_get_user_data(e);
          if (idx < shown.size()) startJoin(shown[idx].ssid, shown[idx].secured);
        },
        LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  if (shown.empty()) {
    lv_obj_t* l = label(ui.list, &jakarta_14_sb, MUTED, "No networks yet. Tap Scan.");
    lv_obj_set_style_pad_top(l, 12, 0);
  }
}

// --- password screen -------------------------------------------------------

const char* const KB_LOWER[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                "ABC", "z", "x", "c", "v", "b", "n", "m", "Del", "\n",
                                "1#", " ", "Done", ""};
const char* const KB_UPPER[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                "abc", "Z", "X", "C", "V", "B", "N", "M", "Del", "\n",
                                "1#", " ", "Done", ""};
const char* const KB_SPECIAL[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                  "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", "\n",
                                  "abc", ".", ",", "?", "!", "'", "_", "#", "%", "+", "=", "Del", "\n",
                                  "*", "<", ">", " ", "[", "]", "Done", ""};
#define W(n) ((lv_buttonmatrix_ctrl_t)(n))
const lv_buttonmatrix_ctrl_t KB_CTRL_TEXT[] = {
    W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(3), W(2), W(2), W(2), W(2), W(2), W(2), W(2), W(3),
    W(2), W(6), W(2)};
const lv_buttonmatrix_ctrl_t KB_CTRL_SPECIAL[] = {
    W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(2), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(1), W(2),
    W(1), W(1), W(1), W(4), W(1), W(1), W(2)};
#undef W

void finishJoin(bool connect) {
  if (connect) net::connect(ui.joining.c_str(), lv_textarea_get_text(ui.textarea));
  lv_textarea_set_text(ui.textarea, "");  // don't leave the password on screen
  if (ui.closePassword) ui.closePassword();
}

void onKey(lv_event_t* e) {
  lv_obj_t* kb = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(kb);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
  const char* t = lv_buttonmatrix_get_button_text(kb, id);
  if (!t) return;
  if (!strcmp(t, "ABC")) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  else if (!strcmp(t, "abc")) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  else if (!strcmp(t, "1#")) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
  else if (!strcmp(t, "Del")) lv_textarea_delete_char(ui.textarea);
  else if (!strcmp(t, "Done")) finishJoin(true);
  else lv_textarea_add_text(ui.textarea, t);
}

void buildPasswordScreen() {
  lv_obj_t* scr = ui.pwScreen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(scr);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, c(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t* head = box(scr);
  lv_obj_set_size(head, 480, 48);
  lv_obj_t* back = box(head);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(back, 56, 48);
  lv_obj_center(icon(back, &icon_back_24, MUTED));
  lv_obj_add_event_cb(back, [](lv_event_t*) { finishJoin(false); }, LV_EVENT_CLICKED, nullptr);
  ui.pwTitle = label(head, &jakarta_20_xb, TEXT, "");
  lv_label_set_long_mode(ui.pwTitle, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(ui.pwTitle, 400);
  lv_obj_align(ui.pwTitle, LV_ALIGN_LEFT_MID, 56, 0);

  ui.textarea = lv_textarea_create(scr);
  lv_obj_set_pos(ui.textarea, 16, 52);
  lv_obj_set_size(ui.textarea, 352, 48);
  lv_textarea_set_one_line(ui.textarea, true);
  lv_textarea_set_password_mode(ui.textarea, true);
  lv_textarea_set_placeholder_text(ui.textarea, "Password");
  lv_textarea_set_max_length(ui.textarea, 63);
  lv_obj_set_style_text_font(ui.textarea, &jakarta_20_sb, 0);
  lv_obj_set_style_text_color(ui.textarea, c(TEXT), 0);
  lv_obj_set_style_text_color(ui.textarea, c(SUBTLE), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ui.textarea, c(BTN_BG), 0);
  lv_obj_set_style_bg_opa(ui.textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ui.textarea, 1, 0);
  lv_obj_set_style_border_color(ui.textarea, c(BTN_BORDER), 0);
  lv_obj_set_style_radius(ui.textarea, 8, 0);
  lv_obj_set_style_pad_all(ui.textarea, 10, 0);
  lv_obj_set_style_bg_color(ui.textarea, c(TEXT), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(ui.textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(ui.textarea, LV_STATE_FOCUSED);

  lv_obj_t* show = pill(scr, 88, "Show", &ui.showLabel, [](lv_event_t*) {
    bool hidden = lv_textarea_get_password_mode(ui.textarea);
    lv_textarea_set_password_mode(ui.textarea, !hidden);
    lv_label_set_text(ui.showLabel, hidden ? "Hide" : "Show");
  });
  lv_obj_set_pos(show, 376, 54);

  lv_obj_t* kb = lv_keyboard_create(scr);
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);  // our maps use text labels
  lv_obj_add_event_cb(kb, onKey, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL_TEXT);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL_TEXT);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SPECIAL, KB_CTRL_SPECIAL);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_popovers(kb, false);
  lv_obj_set_size(kb, 480, 208);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, c(BG), 0);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, 4, 0);
  lv_obj_set_style_pad_gap(kb, 4, 0);
  lv_obj_set_style_bg_color(kb, c(BTN_BG), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, c(BTN_BORDER), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &jakarta_20_sb, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, c(TEXT), LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
}

}  // namespace

lv_obj_t* buildTab(lv_obj_t* parent, void (*openPassword)(), void (*closePassword)()) {
  ui.openPassword = openPassword;
  ui.closePassword = closePassword;

  ui.tab = box(parent);
  lv_obj_set_pos(ui.tab, 16, 48);
  lv_obj_set_size(ui.tab, 448, 272);

  // Status
  lv_obj_t* status = box(ui.tab);
  lv_obj_set_size(status, 448, 64);
  lv_obj_set_style_border_side(status, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(status, 1, 0);
  lv_obj_set_style_border_color(status, c(DIVIDER), 0);
  lv_obj_t* words = box(status);
  lv_obj_set_size(words, 330, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(words, LV_FLEX_FLOW_COLUMN);
  lv_obj_align(words, LV_ALIGN_LEFT_MID, 0, 0);
  ui.title = label(words, &jakarta_20_xb, TEXT, "");
  ui.detail = label(words, &jakarta_14_sb, MUTED, "");
  lv_label_set_long_mode(ui.detail, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(ui.detail, 330);
  ui.forgetBtn = pill(status, 104, "Forget", nullptr, [](lv_event_t*) { net::forget(); });
  lv_obj_align(ui.forgetBtn, LV_ALIGN_RIGHT_MID, 0, 0);

  // Networks header + scan
  lv_obj_t* head = box(ui.tab);
  lv_obj_set_pos(head, 0, 64);
  lv_obj_set_size(head, 448, 56);
  lv_obj_t* t = label(head, &jakarta_14_xb, MUTED, "NETWORKS");
  lv_obj_set_style_text_letter_space(t, 1, 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* scan = pill(head, 120, "Scan", &ui.scanLabel, [](lv_event_t*) { net::requestScan(); });
  lv_obj_align(scan, LV_ALIGN_RIGHT_MID, 0, 0);

  ui.list = box(ui.tab);
  lv_obj_set_pos(ui.list, 0, 120);
  lv_obj_set_size(ui.list, 448, 152);
  lv_obj_set_flex_flow(ui.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(ui.list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(ui.list, LV_DIR_VER);
  rebuildList({});

  buildPasswordScreen();
  return ui.tab;
}

void updateTab() {
  net::Status s = net::status();
  switch (s.link) {
    case net::Link::NotSetUp:
      lv_label_set_text(ui.title, "Not set up");
      lv_label_set_text(ui.detail, "Scan and pick your network");
      break;
    case net::Link::Connecting:
      lv_label_set_text(ui.title, "Connecting…");
      lv_label_set_text(ui.detail, s.ssid);
      break;
    case net::Link::Connected:
      lv_label_set_text(ui.title, "Connected");
      lv_label_set_text_fmt(ui.detail, "%s · %s · %s", s.ssid, s.ip, strength(s.rssi));
      break;
    case net::Link::Disconnected:
      lv_label_set_text(ui.title, "Not connected");
      lv_label_set_text_fmt(ui.detail, "%s · retrying in %lus", s.ssid, (unsigned long)s.retryInS);
      break;
  }
  show(ui.forgetBtn, s.link != net::Link::NotSetUp);

  bool busy = net::scanning();
  lv_label_set_text(ui.scanLabel, busy ? "Scanning" : "Scan");
  if (ui.wasScanning && !busy) {
    rebuildList(net::scanResults());
  }
  ui.wasScanning = busy;
}

lv_obj_t* passwordScreen() { return ui.pwScreen; }

}  // namespace ui::wifi_setup
