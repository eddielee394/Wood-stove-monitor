#include "console.h"

#include <Arduino.h>

#include "buzzer.h"
#include "settings.h"
#include "state.h"

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
  Serial.printf("ideal=%d rate_high=%d reload=%d highest=%s silenced=%d audible=%d sensor_age=%lums\n",
                s.alarms.ideal, s.alarms.rateHigh, s.alarms.reload,
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
    println(on ? "ok: bench mode ON - probe faults silent until reboot"
               : "ok: bench mode off - probe faults sound");
  } else if (!strcmp(cmd, "buzzer")) {
    buzzer::debug();
  } else if (!strcmp(cmd, "silence")) {
    state::requestSilence();
    println("ok: silence requested");
  } else {
    println("commands: show | settings | set <key> <value> | defaults | silence | bench on|off");
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
