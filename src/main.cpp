// Stove monitor - WT32-SC01 Plus
// Phase 4: LVGL UI on top of the Phase 3 core. Sensor and alarm tasks run independently of this
// loop, which runs the LVGL UI and the serial console.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <mbedtls/platform.h>

#include "alarms.h"
#include "buzzer.h"
#include "console.h"
#include "display.h"
#include "mqtt.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "state.h"
#include "ui/ui.h"
#include "version.h"
#include "weather.h"

LGFX tft;

static const uint32_t WDT_TIMEOUT_S = 10;
static const size_t LOW_HEAP = 8000;            // internal RAM bytes
static const uint32_t LOW_HEAP_RESTART_MS = 60000;
static const uint32_t NET_STUCK_RESTART_MS = 600000;  // net.cpp reconnects twice first

// If internal RAM stays nearly exhausted, Wi-Fi can stop working for good
// while everything else runs on. Restart (a few seconds; the alarms come
// straight back) rather than sit unreachable until someone unplugs it.
static void memoryGuard() {
  static uint32_t lowSince = 0;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= LOW_HEAP) {
    lowSince = 0;
  } else if (!lowSince) {
    lowSince = millis() | 1;
  } else if (millis() - lowSince > LOW_HEAP_RESTART_MS) {
    Serial.println("memory: internal RAM exhausted for 60s, restarting");
    state::restartLowMemory();
  }
}

// HTTPS (the weather fetch) needs ~40 KB while it runs. The prebuilt core
// puts it all in internal RAM, which took free internal RAM down to 2 KB and
// could leave Wi-Fi dead. PSRAM has 2 MB spare; use it, internal as fallback.
static void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// Wi-Fi joined but the router silent for 10 min, even after net.cpp
// reconnected: restart, like memoryGuard.
static void networkGuard() {
  if (net::stuckMs() > NET_STUCK_RESTART_MS) {
    Serial.println("net: router unreachable for 10 min, restarting");
    state::restartNetworkStuck();
  }
}

void setup() {
  buzzer::begin();  // pin held quiet before anything else
  Serial.begin(115200);
  delay(500);
  Serial.printf("Stove monitor %s starting (last reset: %s)\n", version::get(), state::resetReason());

  // Reboot if the sensor or alarm task stops checking in.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);

  settings::begin();
  state::begin();
  sensors::start();
  alarms::start();
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  net::start();  // after the safety tasks; nothing depends on it
  weather::start();
  mqtt::start();

  tft.init();
  tft.setRotation(1);  // landscape, 480x320
  ui::begin();
  Serial.println("Type 'help' for console commands");
}

void loop() {
  console::poll();
  ui::loop();
  memoryGuard();
  networkGuard();
  delay(5);
}
