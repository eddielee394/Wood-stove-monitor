// Buzzer driver for the WT32-SC01 Plus stove monitor.
//
// Hardware (verified in Phase 1): active magnetic buzzer on GPIO21, sounds
// when the pin is driven HIGH. It also reproduces LEDC tones cleanly and is
// loudest around 2.5-3.5kHz, so patterns use PWM tones for distinct pitches.
//
// Patterns are stepped by an esp_timer, so play()/stop() return immediately
// and never block the caller (the alarm task in later phases).
#pragma once

#include <stdint.h>

namespace buzzer {

enum class Pattern : uint8_t {
  None,
  Chirp,     // single short beep - touch/ack feedback, plays once
  Warning,   // three beeps, pause, repeat
  Critical,  // rapid continuous beeping, no pause
  Fault,     // long low beeps, repeat
  Reload,    // gentle double chirp every 20s
  Custom,    // one-off tone() in progress
};

// Pass as a tone() frequency to hold the pin HIGH and let the active buzzer's
// built-in oscillator set the pitch.
constexpr uint16_t DC = 0xFFFF;

void begin();
// Start a pattern, replacing whatever is playing. Playing the pattern that is
// already running does not restart it.
void play(Pattern p);
// Play one tone once, replacing whatever is playing.
void tone(uint16_t freq, uint16_t ms);
void stop();
Pattern current();
// Print driver and LEDC/GPIO routing state to Serial (console 'buzzer').
void debug();

}  // namespace buzzer
