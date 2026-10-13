#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>

namespace settings {
namespace {

struct Field {
  const char* key;  // also the NVS key (max 15 chars)
  int32_t Settings::*member;
  int32_t min, max;
};

const Field FIELDS[] = {
    {"stove_warn", &Settings::stoveWarnF, 50, 2300},
    {"stove_crit", &Settings::stoveCritF, 50, 2300},
    {"flue_warn", &Settings::flueWarnF, 50, 2300},
    {"flue_crit", &Settings::flueCritF, 50, 2300},
    {"flue_rate", &Settings::flueRateFpm, 5, 1000},
    {"flue_rate_min", &Settings::flueRateMinF, 0, 2300},
    {"reload_on", &Settings::reloadOn, 0, 1},
    {"reload", &Settings::reloadF, 50, 1000},
    {"hyst", &Settings::hystF, 1, 200},
    {"ideal_on", &Settings::idealOn, 0, 1},
    {"ideal_lo", &Settings::idealLoF, 50, 2300},
    {"ideal_hi", &Settings::idealHiF, 50, 2300},
    {"ideal_exit", &Settings::idealExitF, 50, 2300},
    {"silence_min", &Settings::silenceMin, 1, 120},
    {"silence_crit", &Settings::silenceCritMin, 1, 30},
    {"quiet", &Settings::quiet, 0, 1},
    {"units_c", &Settings::unitsC, 0, 1},
    {"brightness", &Settings::brightness, 10, 255},
};

const char* NVS_NS = "settings";

SemaphoreHandle_t lock;
Settings current;

bool validate(const Settings& s, char* err, size_t errLen) {
  for (const Field& f : FIELDS) {
    int32_t v = s.*f.member;
    if (v < f.min || v > f.max) {
      snprintf(err, errLen, "%s must be %ld-%ld", f.key, (long)f.min, (long)f.max);
      return false;
    }
  }
  if (s.stoveWarnF >= s.stoveCritF) {
    snprintf(err, errLen, "stove_warn must be below stove_crit");
    return false;
  }
  if (s.flueWarnF >= s.flueCritF) {
    snprintf(err, errLen, "flue_warn must be below flue_crit");
    return false;
  }
  if (s.idealLoF >= s.idealHiF) {
    snprintf(err, errLen, "ideal_lo must be below ideal_hi");
    return false;
  }
  if (s.reloadF >= s.stoveWarnF) {
    snprintf(err, errLen, "reload must be below stove_warn");
    return false;
  }
  return true;
}

}  // namespace

// Thresholds confirmed with Eddie (catalytic stove, double-wall pipe).
Settings defaults() {
  Settings s;
  s.stoveWarnF = 1000;
  s.stoveCritF = 1100;
  s.flueWarnF = 850;
  s.flueCritF = 950;
  s.flueRateFpm = 150;   // starting point; tune from real burns
  s.flueRateMinF = 500;  // ignore fast climbs from a cold start
  s.reloadOn = 1;
  s.reloadF = 300;
  s.hystF = 25;
  // Ideal burn: catalytic cruising range at the collar (in-pipe probe
  // guidance is 275-500F; catalytic stoves cruise ~400-550F) and a chimney
  // exit above ~250F so water vapour and creosote don't condense.
  s.idealOn = 1;
  s.idealLoF = 300;
  s.idealHiF = 550;
  s.idealExitF = 250;
  s.silenceMin = 10;
  s.silenceCritMin = 2;
  s.quiet = 0;
  s.unitsC = 0;
  s.brightness = 200;
  return s;
}

void begin() {
  lock = xSemaphoreCreateMutex();
  Settings s = defaults();
  Preferences p;
  p.begin(NVS_NS, true);
  for (const Field& f : FIELDS) s.*f.member = p.getInt(f.key, s.*f.member);
  p.end();

  char err[64];
  if (!validate(s, err, sizeof(err))) {
    Serial.printf("settings: stored values invalid (%s), using defaults\n", err);
    s = defaults();
  }
  current = s;
}

Settings get() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Settings s = current;
  xSemaphoreGive(lock);
  return s;
}

bool apply(const Settings& s, char* err, size_t errLen) {
  if (!validate(s, err, errLen)) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  Preferences p;
  p.begin(NVS_NS, false);
  for (const Field& f : FIELDS) {
    if (current.*f.member != s.*f.member) p.putInt(f.key, s.*f.member);
  }
  p.end();
  current = s;
  xSemaphoreGive(lock);
  return true;
}

bool assign(Settings& s, const char* key, long value) {
  for (const Field& f : FIELDS) {
    if (strcmp(key, f.key) == 0) {
      s.*f.member = value;
      return true;
    }
  }
  return false;
}

bool setByName(const char* key, long value, char* err, size_t errLen) {
  Settings s = get();
  if (!assign(s, key, value)) {
    snprintf(err, errLen, "unknown setting '%s'", key);
    return false;
  }
  return apply(s, err, errLen);
}

void forEach(const Settings& s, void (*fn)(const char*, long, long, long, void*), void* ctx) {
  for (const Field& f : FIELDS) fn(f.key, s.*f.member, f.min, f.max, ctx);
}

void describe(const Settings& s, void (*line)(const char*)) {
  char buf[64];
  for (const Field& f : FIELDS) {
    snprintf(buf, sizeof(buf), "%-13s = %-5ld (%ld-%ld)", f.key,
             (long)(s.*f.member), (long)f.min, (long)f.max);
    line(buf);
  }
}

}  // namespace settings
