#include "buzzer.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/semphr.h>

namespace buzzer {
namespace {

const int PIN = 21;
// LEDC channel 0 runs on timer 0; the backlight uses channel 7 (timer 3).
const int CHANNEL = 0;
const int RES_BITS = 8;
const uint32_t DUTY_ON = 128;              // 50% square wave
const uint32_t DUTY_FULL = 1 << RES_BITS;  // pin held HIGH

struct Step {
  uint16_t freq;  // 0 = silent, DC = steady HIGH (buzzer's own oscillator)
  uint16_t ms;
};

// Tuned by ear with Eddie in Phase 1. Warning and Critical share the loud
// 2.7kHz pitch and differ by rhythm; each pattern must stay distinct.
const Step CHIRP[] = {{2700, 80}};
const Step WARNING[] = {{2700, 150}, {0, 100}, {2700, 150}, {0, 100},
                        {2700, 150}, {0, 1000}};
const Step CRITICAL[] = {{2700, 100}, {0, 60}};
const Step FAULT[] = {{2200, 600}, {0, 600}};
Step custom[] = {{0, 0}};  // filled by tone()

struct Def {
  const Step* steps;
  uint8_t count;
  bool repeat;
};

#define DEF(arr, rep) {arr, sizeof(arr) / sizeof(arr[0]), rep}
const Def DEFS[] = {
    {nullptr, 0, false},   // None
    DEF(CHIRP, false),
    DEF(WARNING, true),
    DEF(CRITICAL, true),
    DEF(FAULT, true),
    DEF(custom, false),
};
#undef DEF

SemaphoreHandle_t lock;
esp_timer_handle_t timer;
Pattern pattern = Pattern::None;
uint8_t step = 0;
int64_t stepEndUs = 0;

void silence() { ledcWrite(CHANNEL, 0); }

// Caller holds the lock.
void startStep() {
  const Step& s = DEFS[(int)pattern].steps[step];
  if (s.freq == DC) {
    ledcWrite(CHANNEL, DUTY_FULL);
  } else if (s.freq) {
    ledcChangeFrequency(CHANNEL, s.freq, RES_BITS);
    ledcWrite(CHANNEL, DUTY_ON);
  } else {
    silence();
  }
  stepEndUs = esp_timer_get_time() + s.ms * 1000LL;
  esp_timer_start_once(timer, s.ms * 1000ULL);
}

void onTimer(void*) {
  xSemaphoreTake(lock, portMAX_DELAY);
  // A callback that was already queued when play()/stop() changed the
  // pattern fires before the new step's end time; ignore it.
  if (pattern != Pattern::None && esp_timer_get_time() >= stepEndUs) {
    const Def& d = DEFS[(int)pattern];
    if (++step >= d.count) {
      if (d.repeat) {
        step = 0;
      } else {
        pattern = Pattern::None;
        silence();
      }
    }
    if (pattern != Pattern::None) startStep();
  }
  xSemaphoreGive(lock);
}

}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  ledcSetup(CHANNEL, 2700, RES_BITS);
  ledcAttachPin(PIN, CHANNEL);
  silence();  // pin held LOW = quiet

  esp_timer_create_args_t args = {};
  args.callback = onTimer;
  args.name = "buzzer";
  esp_timer_create(&args, &timer);
}

void play(Pattern p) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (p != pattern) {
    esp_timer_stop(timer);
    pattern = p;
    step = 0;
    if (p == Pattern::None) {
      silence();
    } else {
      startStep();
    }
  }
  xSemaphoreGive(lock);
}

void tone(uint16_t freq, uint16_t ms) {
  xSemaphoreTake(lock, portMAX_DELAY);
  esp_timer_stop(timer);
  custom[0] = {freq, ms};
  pattern = Pattern::Custom;
  step = 0;
  startStep();
  xSemaphoreGive(lock);
}

void stop() { play(Pattern::None); }

Pattern current() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Pattern p = pattern;
  xSemaphoreGive(lock);
  return p;
}

}  // namespace buzzer
