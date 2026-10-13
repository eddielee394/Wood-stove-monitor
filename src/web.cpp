#include "web.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <libb64/cdecode.h>

#include "net.h"
#include "ota.h"
#include "settings.h"
#include "state.h"
#include "web_page.h"

namespace web {
namespace {

WebServer server(80);
bool started = false;

// Small append-only JSON writer into a String.
struct Json {
  String s;
  bool first = true;
  void sep() {
    if (!first) s += ',';
    first = false;
  }
  void key(const char* k) {
    sep();
    s += '"';
    s += k;
    s += "\":";
  }
  void num(const char* k, float v, int decimals = 1) {
    key(k);
    if (isnan(v)) s += "null";
    else s += String(v, decimals);
  }
  void integer(const char* k, long v) {
    key(k);
    s += v;
  }
  void boolean(const char* k, bool v) {
    key(k);
    s += v ? "true" : "false";
  }
  void str(const char* k, const char* v) {
    key(k);
    s += '"';
    for (const char* p = v; *p; p++) {
      if (*p == '"' || *p == '\\') s += '\\';
      if ((uint8_t)*p >= 0x20) s += *p;
    }
    s += '"';
  }
  void open(const char* k, char bracket) {
    if (k) key(k);
    else sep();
    s += bracket;
    first = true;
  }
  void close(char bracket) {
    s += bracket;
    first = false;
  }
};

void settingsJson(Json& j, const settings::Settings& cfg) {
  j.open("settings", '{');
  settings::forEach(
      cfg,
      [](const char* k, long v, long, long, void* ctx) { static_cast<Json*>(ctx)->integer(k, v); },
      &j);
  j.close('}');
}

void handleStatus() {
  state::Snapshot s = state::get();
  settings::Settings cfg = settings::get();
  net::Status ns = net::status();
  Json j;
  j.s.reserve(1200);
  j.open(nullptr, '{');
  j.open("ch", '[');
  for (int i = 0; i < thermo::COUNT; i++) {
    const state::Channel& c = s.ch[i];
    j.open(nullptr, '{');
    j.boolean("valid", c.valid);
    j.boolean("present", c.present);
    j.num("temp", c.tempF);
    j.num("rate", c.rateFpm);
    j.num("board", c.boardF);
    j.integer("fault_raw", c.fault);
    j.close('}');
  }
  j.close(']');
  const state::Alarms& a = s.alarms;
  j.open("alarms", '{');
  j.open("temp", '[');
  for (int i = 0; i < thermo::COUNT; i++) {
    j.sep();
    j.s += '"';
    j.s += state::levelName(a.temp[i]);
    j.s += '"';
  }
  j.close(']');
  j.open("fault", '[');
  for (int i = 0; i < thermo::COUNT; i++) {
    j.sep();
    j.s += a.fault[i] ? "true" : "false";
  }
  j.close(']');
  j.boolean("rate_high", a.rateHigh);
  j.boolean("reload", a.reload);
  j.boolean("ideal", a.ideal);
  j.str("highest", state::levelName(a.highest));
  j.boolean("silenced", a.silenced);
  j.integer("silence_left", a.silenceLeftS);
  j.boolean("audible", a.audible);
  j.close('}');
  settingsJson(j, cfg);
  j.open("wifi", '{');
  j.str("ssid", ns.ssid);
  j.integer("rssi", ns.rssi);
  j.str("ip", ns.ip);
  j.close('}');
  j.integer("uptime", millis() / 1000);
  j.str("reset_reason", state::resetReason());
  j.integer("sensor_age_ms", s.sensorMs ? millis() - s.sensorMs : -1);
  j.close('}');
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j.s);
}

// Apply all submitted fields as one validated change, so a form that moves
// both warning and critical can't be rejected halfway through.
void handleSettings() {
  settings::Settings next = settings::get();
  char err[96] = "";
  bool ok = true;
  for (int i = 0; i < server.args() && ok; i++) {
    String k = server.argName(i);
    if (k == "plain") continue;  // raw body, already parsed into args
    char* end = nullptr;
    long v = strtol(server.arg(i).c_str(), &end, 10);
    if (server.arg(i).isEmpty() || *end) {
      snprintf(err, sizeof(err), "%s: not a whole number", k.c_str());
      ok = false;
    } else if (!settings::assign(next, k.c_str(), v)) {
      snprintf(err, sizeof(err), "unknown setting '%s'", k.c_str());
      ok = false;
    }
  }
  if (ok) ok = settings::apply(next, err, sizeof(err));
  Json j;
  j.open(nullptr, '{');
  j.boolean("ok", ok);
  if (!ok) j.str("error", err);
  settingsJson(j, settings::get());
  j.close('}');
  server.send(ok ? 200 : 400, "application/json", j.s);
}

bool uploadAllowed = false;

// HTTP Basic auth "ota:<password>", checked against NVS.
bool otaAuthorised() {
  if (!ota::enabled() || !server.hasHeader("Authorization")) return false;
  String h = server.header("Authorization");
  if (!h.startsWith("Basic ")) return false;
  String enc = h.substring(6);
  char dec[128] = {};
  if (enc.length() > 160) return false;
  int n = base64_decode_chars(enc.c_str(), enc.length(), dec);
  if (n <= 0 || n >= (int)sizeof(dec)) return false;
  dec[n] = '\0';
  char* colon = strchr(dec, ':');
  bool ok = colon && (colon - dec) == 3 && !strncmp(dec, "ota", 3) && ota::checkPassword(colon + 1);
  memset(dec, 0, sizeof(dec));
  return ok;
}

void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadAllowed = otaAuthorised();
    if (!uploadAllowed) {
      Serial.println("ota: rejected upload (bad or missing password)");
      return;
    }
    // Content-Length includes the multipart framing; close enough for progress.
    ota::startUpdate(server.header("Content-Length").toInt());
  } else if (!uploadAllowed || ota::progress() < 0) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    ota::write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    ota::finish();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    ota::abort();
  }
}

}  // namespace

void begin() {
  if (started) return;
  started = true;
  const char* headers[] = {"Authorization", "Content-Length"};
  server.collectHeaders(headers, 2);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/silence", HTTP_POST, []() {
    state::requestSilence();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  // Firmware update: POST multipart "firmware" file, Basic auth user "ota".
  server.on(
      "/update", HTTP_POST,
      []() {
        if (!uploadAllowed) {
          server.send(401, "text/plain", "Unauthorized\n");
          return;
        }
        bool ok = ota::progress() == 100;
        server.send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting\n" : ota::error());
        if (ok) {
          delay(500);
          ESP.restart();
        }
      },
      handleUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  if (MDNS.begin("stove-monitor")) {
    MDNS.addService("http", "tcp", 80);
  }
  Serial.println("web: listening on port 80 (http://stove-monitor.local)");
}

void handle() {
  if (started) server.handleClient();
}

}  // namespace web
