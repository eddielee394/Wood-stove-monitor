// User settings, persisted in NVS. Every change - touchscreen, serial console,
// web page, MQTT - must go through apply() or setByName(), which validate the
// whole set before anything is stored.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

// Temperatures are always stored in deg F; unitsC only affects display.
struct Settings {
  int32_t stoveWarnF;   // STOVE probe (flue just above the collar)
  int32_t stoveCritF;
  int32_t flueWarnF;    // FLUE probe (top of the chimney)
  int32_t flueCritF;
  int32_t flueRateFpm;  // FLUE rate-of-rise alarm, deg F per minute
  int32_t flueRateMinF; // rate alarm can only start above this FLUE temp
  int32_t reloadOn;     // 0/1: low "time to reload" alert on STOVE
  int32_t reloadF;
  int32_t hystF;        // hysteresis applied to every threshold
  int32_t idealOn;      // 0/1: show the "ideal burn" indicator
  int32_t idealLoF;     // ideal when STOVE is within lo..hi
  int32_t idealHiF;
  int32_t idealExitF;   // ...and FLUE (chimney exit) is at least this
  int32_t silenceMin;   // a silenced alarm re-arms after this long
  int32_t silenceCritMin;  // ...or this long for Critical / chimney fire
  int32_t quiet;        // 0/1: mute Warning and Reload (never Critical/Fault)
  int32_t unitsC;       // 0 = deg F, 1 = deg C
  int32_t brightness;   // backlight 0-255
};

Settings defaults();
void begin();  // load from NVS; missing keys take defaults
Settings get();

// Validate and persist. On failure nothing changes and err explains why.
bool apply(const Settings& s, char* err, size_t errLen);
// Set one field of `s` by key name, without validating or saving. Returns
// false for an unknown key. Pass the result to apply().
bool assign(Settings& s, const char* key, long value);
// Change one field by key name (as listed by describe()).
bool setByName(const char* key, long value, char* err, size_t errLen);
// Visit every field (key, current value, allowed range), e.g. for JSON.
void forEach(const Settings& s, void (*fn)(const char* key, long value, long min, long max, void* ctx),
             void* ctx);
// One "key = value  (min-max)" line per field, for the serial console.
void describe(const Settings& s, void (*line)(const char*));

}  // namespace settings
