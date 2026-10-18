#include "console.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "buzzer.h"
#include "mqtt.h"
#include "ota.h"
#include "settings.h"
#include "state.h"
#include "version.h"
#include "weather.h"

namespace console {
namespace {

char buf[96];
size_t len = 0;

void println(const char* s) { Serial.println(s); }

void show() {
  state::Snapshot s = state::get();
  for (int i = 0; i < thermo::COUNT; i++) {
    const state::Channel& c = s.ch[i];
    Serial.printf("%-5s temp=%.1fF rate=%.1fF/min board=%.1fF fault=%s raw=0x%02X present=%d alarm=%s%s\n",
                  thermo::name((thermo::Channel)i), c.tempF, c.rateFpm, c.boardF,
                  c.faulted ? "YES" : "no", c.fault, c.present,
                  state::levelName(s.alarms.temp[i]), s.alarms.fault[i] ? " +FAULT" : "");
  }
  Serial.printf("firmware %s, last reset: %s, up %lus\n", version::get(), state::resetReason(),
                (unsigned long)(millis() / 1000));
  Serial.printf("ideal=%d rate_high=%d reload=%d armed=%d highest=%s silenced=%d audible=%d sensor_age=%lums\n",
                s.alarms.ideal, s.alarms.rateHigh, s.alarms.reload, s.alarms.reloadArmed,
                state::levelName(s.alarms.highest),
                s.alarms.silenced, s.alarms.audible,
                s.sensorMs ? (unsigned long)(millis() - s.sensorMs) : 0UL);
}

void run(char* line) {
  char* cmd = strtok(line, " ");
  if (!cmd) return;
  char err[64];

  if (!strcmp(cmd, "show")) {
    show();
  } else if (!strcmp(cmd, "settings")) {
    settings::describe(settings::get(), println);
  } else if (!strcmp(cmd, "set")) {
    char* key = strtok(nullptr, " ");
    char* val = strtok(nullptr, " ");
    char* end = nullptr;
    long v = val ? strtol(val, &end, 10) : 0;
    if (!key || !val || *end) {
      println("usage: set <key> <integer>");
    } else if (settings::setByName(key, v, err, sizeof(err))) {
      Serial.printf("ok: %s = %ld (saved)\n", key, v);
    } else {
      Serial.printf("rejected: %s\n", err);
    }
  } else if (!strcmp(cmd, "defaults")) {
    if (settings::apply(settings::defaults(), err, sizeof(err))) {
      println("ok: defaults restored (saved)");
    }
  } else if (!strcmp(cmd, "bench")) {
    char* arg = strtok(nullptr, " ");
    bool on = arg && !strcmp(arg, "on");
    state::setBenchMuteFaults(on);
    println(on ? "ok: bench mode ON - probe faults silent, fault strip hidden, until reboot"
               : "ok: bench mode off - probe faults sound");
  } else if (!strcmp(cmd, "otapass")) {
    char* pw = strtok(nullptr, " ");
    if (pw && strlen(pw) >= 12) {
      ota::setPassword(pw);  // never echoed
      println("ok: OTA password saved");
    } else if (pw) {
      println("rejected: use at least 12 characters");
    } else {
      println(ota::hasPassword() ? "OTA password is set" : "OTA password is NOT set (OTA disabled)");
    }
  } else if (!strcmp(cmd, "location")) {
    char* a = strtok(nullptr, " ");
    char* b = strtok(nullptr, " ");
    if (a && !strcmp(a, "clear")) {
      weather::clearLocation();
      println("ok: location cleared (outside temperature off)");
    } else if (a && b && weather::setLocation(atof(a), atof(b))) {
      println("ok: location saved, fetching weather");
    } else {
      println("usage: location <latitude> <longitude> | location clear");
    }
  } else if (!strcmp(cmd, "weather")) {
    weather::Status w = weather::status();
    Serial.printf("located=%d valid=%d temp=%.1fF sky=%s day=%d age=%lus sunrise=%s sunset=%s error=%s\n",
                  w.located, w.valid, w.tempF, weather::skyName(w.sky), w.isDay,
                  (unsigned long)w.ageS, w.sunrise, w.sunset, w.error);
  } else if (!strcmp(cmd, "mqtt")) {
    mqtt::Status m = mqtt::status();
    Serial.printf("mqtt %s broker=%s:%u user=%s password=%s error=%s\n", mqtt::connName(m.conn),
                  m.host, m.port, m.user, m.hasPass ? "set" : "none", m.error);
  } else if (!strcmp(cmd, "mem")) {
    Serial.printf("heap: internal free %u (min %u, largest %u), psram free %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else if (!strcmp(cmd, "buzzer")) {
    buzzer::debug();
  } else if (!strcmp(cmd, "silence")) {
    state::requestSilence();
    println("ok: silence requested");
  } else {
    println("commands: show | settings | set <key> <value> | defaults | silence | bench on|off | weather | location | mqtt");
  }
}

}  // namespace

void poll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      run(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

}  // namespace console
