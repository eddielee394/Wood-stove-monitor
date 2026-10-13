#include "state.h"

#include <Arduino.h>
#include <freertos/semphr.h>

#include <esp_system.h>

#include <atomic>

namespace state {
namespace {

SemaphoreHandle_t lock;
Snapshot snap;
std::atomic<bool> silenceRequested{false};
std::atomic<bool> benchMute{false};

}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  snap = {};
  for (auto& c : snap.ch) c.tempF = c.rateFpm = c.boardF = NAN;
}

Snapshot get() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Snapshot s = snap;
  xSemaphoreGive(lock);
  return s;
}

void setChannels(const Channel (&ch)[thermo::COUNT]) {
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < thermo::COUNT; i++) snap.ch[i] = ch[i];
  snap.sensorMs = millis();
  xSemaphoreGive(lock);
}

void setAlarms(const Alarms& a) {
  xSemaphoreTake(lock, portMAX_DELAY);
  snap.alarms = a;
  xSemaphoreGive(lock);
}

void requestSilence() { silenceRequested = true; }
bool takeSilenceRequest() { return silenceRequested.exchange(false); }

void setBenchMuteFaults(bool on) { benchMute = on; }
bool benchMuteFaults() { return benchMute; }

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT: return "reset pin";
    default: return "other";
  }
}

const char* levelName(Level l) {
  switch (l) {
    case Level::None: return "NONE";
    case Level::Reload: return "RELOAD";
    case Level::Warning: return "WARNING";
    case Level::Fault: return "FAULT";
    case Level::Critical: return "CRITICAL";
  }
  return "?";
}

}  // namespace state
