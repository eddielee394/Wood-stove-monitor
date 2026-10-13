#include "sensors.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "state.h"
#include "thermo.h"

namespace sensors {
namespace {

const uint32_t PERIOD_MS = 1000;
const float ALPHA = 0.5f;          // EMA weight of the newest reading
const int FAULT_AFTER = 3;         // consecutive bad reads before a fault
const int RECOVER_AFTER = 2;       // consecutive good reads to clear it
const int HISTORY = 61;            // 60s rate window at 1Hz
const uint32_t MIN_RATE_SPAN_MS = 30000;

struct Track {
  int bad = 0, good = 0;
  float ema = NAN;
  float temps[HISTORY];
  uint32_t times[HISTORY];
  int head = 0, count = 0;

  void resetHistory() {
    ema = NAN;
    head = count = 0;
  }

  void push(float f, uint32_t ms) {
    temps[head] = f;
    times[head] = ms;
    head = (head + 1) % HISTORY;
    if (count < HISTORY) count++;
  }

  float rate() const {
    if (count < 2) return NAN;
    int newest = (head + HISTORY - 1) % HISTORY;
    int oldest = (head + HISTORY - count) % HISTORY;
    uint32_t span = times[newest] - times[oldest];
    if (span < MIN_RATE_SPAN_MS) return NAN;
    return (temps[newest] - temps[oldest]) * 60000.0f / span;
  }
};

float toF(float c) { return c * 9.0f / 5.0f + 32.0f; }

void task(void*) {
  esp_task_wdt_add(nullptr);
  thermo::begin();

  Track tracks[thermo::COUNT];
  state::Channel ch[thermo::COUNT] = {};
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    uint32_t now = millis();
    for (int i = 0; i < thermo::COUNT; i++) {
      Track& t = tracks[i];
      state::Channel& c = ch[i];
      thermo::Reading r = thermo::read((thermo::Channel)i);
      bool good = r.present && r.fault == 0 && !isnan(r.tcC);

      if (good) {
        t.good++;
        t.bad = 0;
      } else {
        t.bad++;
        t.good = 0;
      }
      if (!c.faulted && t.bad >= FAULT_AFTER) {
        c.faulted = true;
        t.resetHistory();  // never show a stale temperature after a fault
      } else if (c.faulted && t.good >= RECOVER_AFTER) {
        c.faulted = false;
      }

      // A single bad read (below FAULT_AFTER) holds the last smoothed value.
      if (good && !c.faulted) {
        float f = toF(r.tcC);
        t.ema = isnan(t.ema) ? f : t.ema + ALPHA * (f - t.ema);
        t.push(t.ema, now);
      }

      c.valid = !c.faulted && !isnan(t.ema);
      c.tempF = c.valid ? t.ema : NAN;
      c.rateFpm = c.valid ? t.rate() : NAN;
      c.boardF = r.present ? toF(r.cjC) : NAN;
      c.fault = r.fault;
      c.present = r.present;
    }
    state::setChannels(ch);
    esp_task_wdt_reset();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
  }
}

}  // namespace

void start() {
  xTaskCreatePinnedToCore(task, "sensors", 4096, nullptr, 5, nullptr, 1);
}

}  // namespace sensors
