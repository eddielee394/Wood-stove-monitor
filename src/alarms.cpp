#include "alarms.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "buzzer.h"
#include "settings.h"
#include "state.h"

namespace alarms {
namespace {

using state::Level;

const uint32_t PERIOD_MS = 250;
const uint32_t STALE_MS = 5000;           // sensor data older than this = fault
const uint32_t RELOAD_AUDIBLE_MS = 5 * 60000;  // then visual only
const float RATE_HYST_FRACTION = 0.2f;  // clears 20% below the limit
const float IDEAL_DEADBAND_F = 10;      // keeps the indicator from flickering

// Warning/critical with hysteresis: a level holds until the temperature
// falls hyst below the threshold that raised it.
Level tempLevel(Level prev, float t, float warn, float crit, float hyst) {
  if (isnan(t)) return Level::None;  // faults are handled separately
  if (t >= crit || (prev == Level::Critical && t > crit - hyst)) return Level::Critical;
  if (t >= warn || (prev >= Level::Warning && t > warn - hyst)) return Level::Warning;
  return Level::None;
}

// One bit per alarm condition, so a silence acknowledges exactly the
// conditions active at that moment.
enum : uint16_t {
  C_WARN = 1 << 0,    // + probe index
  C_CRIT = 1 << 2,    // + probe index
  C_FAULT = 1 << 4,   // + probe index
  C_RATE = 1 << 6,
  C_RELOAD = 1 << 7,
};

uint16_t conditions(const state::Alarms& a) {
  uint16_t m = 0;
  for (int i = 0; i < thermo::COUNT; i++) {
    if (a.temp[i] >= Level::Warning) m |= C_WARN << i;  // critical keeps its warning bit
    if (a.temp[i] == Level::Critical) m |= C_CRIT << i;
    if (a.fault[i]) m |= C_FAULT << i;
  }
  if (a.rateHigh) m |= C_RATE;
  if (a.reload) m |= C_RELOAD;
  return m;
}

buzzer::Pattern patternFor(Level l) {
  switch (l) {
    case Level::Critical: return buzzer::Pattern::Critical;
    case Level::Fault: return buzzer::Pattern::Fault;
    case Level::Warning: return buzzer::Pattern::Warning;
    case Level::Reload: return buzzer::Pattern::Reload;
    default: return buzzer::Pattern::None;
  }
}

void task(void*) {
  esp_task_wdt_add(nullptr);

  state::Alarms a = {};
  bool reloadArmed = false;
  uint32_t reloadSince = 0;
  uint16_t acked = 0;  // conditions covered by the current silence
  uint32_t silencedAt = 0;
  uint32_t silenceMs = 0;

  for (;;) {
    state::Snapshot snap = state::get();
    settings::Settings s = settings::get();
    uint32_t now = millis();

    // Allow the sensor task a few seconds to deliver its first reading.
    bool stale = now > STALE_MS && (snap.sensorMs == 0 || now - snap.sensorMs > STALE_MS);

    const float warn[] = {(float)s.stoveWarnF, (float)s.flueWarnF};
    const float crit[] = {(float)s.stoveCritF, (float)s.flueCritF};
    for (int i = 0; i < thermo::COUNT; i++) {
      a.fault[i] = stale || snap.ch[i].faulted;
      a.temp[i] = stale ? Level::None
                        : tempLevel(a.temp[i], snap.ch[i].tempF, warn[i], crit[i], s.hystF);
    }

    // Rate-of-rise can only start once the flue is hot (cold-start climbs are
    // normal); once raised it clears on rate alone.
    float rate = snap.ch[thermo::FLUE].rateFpm;
    float flue = snap.ch[thermo::FLUE].tempF;
    float rateClear = s.flueRateFpm * (1.0f - RATE_HYST_FRACTION);
    bool rateStart = rate >= s.flueRateFpm && flue >= s.flueRateMinF;
    a.rateHigh = !stale && !isnan(rate) && (rateStart || (a.rateHigh && rate > rateClear));

    // Reload: arms once the stove has been comfortably above the threshold,
    // fires when it cools below it. Acknowledging it disarms until next hot.
    float stove = snap.ch[thermo::STOVE].tempF;
    if (!s.reloadOn) {
      reloadArmed = a.reload = false;
    } else if (!isnan(stove)) {
      if (stove >= s.reloadF + 2 * s.hystF) {
        reloadArmed = true;
        a.reload = false;
      } else if (reloadArmed && stove < s.reloadF && !a.reload) {
        a.reload = true;
        reloadSince = now;
      }
    }

    // Ideal-burn indicator: informational only, never raises an alarm.
    float flueT = snap.ch[thermo::FLUE].tempF;
    if (!s.idealOn || stale || isnan(stove) || isnan(flueT)) {
      a.ideal = false;
    } else {
      float d = a.ideal ? IDEAL_DEADBAND_F : 0;
      a.ideal = stove >= s.idealLoF - d && stove <= s.idealHiF + d &&
                flueT >= s.idealExitF - d;
    }

    a.reloadArmed = reloadArmed;

    Level highest = Level::None;
    auto raise = [&](Level l) { if (l > highest) highest = l; };
    for (int i = 0; i < thermo::COUNT; i++) {
      raise(a.temp[i]);
      if (a.fault[i]) raise(Level::Fault);
    }
    if (a.rateHigh) raise(Level::Critical);
    if (a.reload) raise(Level::Reload);

    if (state::takeSilenceRequest()) {
      if (a.reload) {
        a.reload = false;
        reloadArmed = a.reloadArmed = false;
        if (highest == Level::Reload) highest = Level::None;
      }
      if (highest != Level::None) {
        a.silenced = true;
        acked = conditions(a);
        silencedAt = now;
        // Critical (incl. chimney fire) gets a short silence; the rest longer.
        uint32_t min = highest == Level::Critical ? s.silenceCritMin : s.silenceMin;
        silenceMs = min * 60000;
      }
    }
    if (a.silenced) {
      // A condition that clears drops out of the acknowledgement, so if it
      // comes back it counts as new. Any new condition - a worsening, the
      // other probe, a fault, a reload - re-arms immediately, as does the
      // timeout.
      uint16_t active = conditions(a);
      acked &= active;
      if (active == 0 || (active & ~acked) || now - silencedAt >= silenceMs) {
        a.silenced = false;
      }
    }
    a.silenceLeftS = a.silenced ? (silenceMs - (now - silencedAt) + 999) / 1000 : 0;
    a.highest = highest;

    buzzer::Pattern p = a.silenced ? buzzer::Pattern::None : patternFor(highest);
    if (p == buzzer::Pattern::Reload && now - reloadSince >= RELOAD_AUDIBLE_MS) {
      p = buzzer::Pattern::None;
    }
    // Quiet mode mutes only the non-urgent patterns; Critical and Fault
    // always sound.
    if (s.quiet && (p == buzzer::Pattern::Warning || p == buzzer::Pattern::Reload)) {
      p = buzzer::Pattern::None;
    }
    if (p == buzzer::Pattern::Fault && state::benchMuteFaults()) p = buzzer::Pattern::None;
    buzzer::play(p);
    a.audible = p != buzzer::Pattern::None;

    state::setAlarms(a);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
  }
}

}  // namespace

void start() {
  xTaskCreatePinnedToCore(task, "alarms", 4096, nullptr, 4, nullptr, 1);
}

}  // namespace alarms
