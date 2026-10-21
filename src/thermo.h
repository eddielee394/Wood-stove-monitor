// Thermocouple front end: two Adafruit MAX31856 breakouts on FSPI.
// Each chip is configured for type K, 60Hz rejection, continuous conversion
// and open-circuit fault detection.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace thermo {

enum Channel : uint8_t { STOVE = 0, FLUE = 1, COUNT = 2 };

struct Reading {
  bool present;   // chip answered on SPI with the expected config
  float tcC;      // thermocouple temperature, deg C (NAN if not present)
  float cjC;      // cold-junction (board) temperature, deg C
  uint8_t fault;  // MAX31856 fault status register (0 = OK)
};

void begin();
Reading read(Channel ch);
const char* name(Channel ch);
// Short human-readable list of the fault bits set, e.g. "OPEN OVUV".
void describeFault(uint8_t fault, char* out, size_t len);

// What made a read bad, beyond the fault register bits (state::Channel::lastBad).
const uint16_t BAD_NO_CHIP = 0x100;   // chip not answering on SPI
const uint16_t BAD_NO_VALUE = 0x200;  // no fault bits but no temperature either
uint16_t badCode(const Reading& r);   // 0 when the read is good
// Plain words for Home Assistant: "open circuit", "voltage", "out of range"...
void describeBad(uint16_t bad, char* out, size_t len);

}  // namespace thermo
