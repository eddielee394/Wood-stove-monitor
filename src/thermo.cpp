#include "thermo.h"

#include <Adafruit_MAX31856.h>
#include <Arduino.h>
#include <SPI.h>

namespace thermo {
namespace {

// I/O header pins (FSPI / SPI2 native pins)
const int PIN_SCK = 12;
const int PIN_MISO = 13;
const int PIN_MOSI = 11;
const int PIN_CS[COUNT] = {10, 14};  // stove, flue

Adafruit_MAX31856 chips[COUNT] = {
    Adafruit_MAX31856(PIN_CS[STOVE], &SPI),
    Adafruit_MAX31856(PIN_CS[FLUE], &SPI),
};

// A missing chip reads back 0x00 or 0xFF, never type K.
bool configured(Channel ch) {
  return chips[ch].getThermocoupleType() == MAX31856_TCTYPE_K;
}

void configure(Channel ch) {
  Adafruit_MAX31856& c = chips[ch];
  c.begin();  // sets type K, open-circuit detection, one-shot mode
  // The filter can only change while conversions are stopped.
  c.setNoiseFilter(MAX31856_NOISE_FILTER_60HZ);
  c.setConversionMode(MAX31856_CONTINUOUS);
}

}  // namespace

void begin() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  for (int i = 0; i < COUNT; i++) configure((Channel)i);
}

Reading read(Channel ch) {
  Reading r = {};
  r.present = configured(ch);
  if (!r.present) {
    // Chip may have browned out or been reconnected; try to bring it back.
    configure(ch);
    r.present = configured(ch);
  }
  if (!r.present) {
    r.tcC = r.cjC = NAN;
    return r;
  }
  r.tcC = chips[ch].readThermocoupleTemperature();
  r.cjC = chips[ch].readCJTemperature();
  r.fault = chips[ch].readFault();
  return r;
}

const char* name(Channel ch) { return ch == STOVE ? "STOVE" : "FLUE"; }

void describeFault(uint8_t fault, char* out, size_t len) {
  static const struct { uint8_t bit; const char* name; } BITS[] = {
      {MAX31856_FAULT_OPEN, "OPEN"},       {MAX31856_FAULT_OVUV, "OVUV"},
      {MAX31856_FAULT_TCRANGE, "TCRANGE"}, {MAX31856_FAULT_CJRANGE, "CJRANGE"},
      {MAX31856_FAULT_TCHIGH, "TCHIGH"},   {MAX31856_FAULT_TCLOW, "TCLOW"},
      {MAX31856_FAULT_CJHIGH, "CJHIGH"},   {MAX31856_FAULT_CJLOW, "CJLOW"},
  };
  out[0] = '\0';
  for (const auto& b : BITS) {
    if (fault & b.bit) {
      if (out[0]) strlcat(out, " ", len);
      strlcat(out, b.name, len);
    }
  }
  if (!out[0]) strlcpy(out, "OK", len);
}

uint16_t badCode(const Reading& r) {
  if (!r.present) return BAD_NO_CHIP;
  if (r.fault) return r.fault;
  return isnan(r.tcC) ? BAD_NO_VALUE : 0;
}

void describeBad(uint16_t bad, char* out, size_t len) {
  static const struct { uint16_t bits; const char* name; } KINDS[] = {
      {BAD_NO_CHIP, "chip not answering"},
      {BAD_NO_VALUE, "no reading"},
      {MAX31856_FAULT_OPEN, "open circuit"},
      {MAX31856_FAULT_OVUV, "voltage"},
      {MAX31856_FAULT_TCRANGE | MAX31856_FAULT_TCHIGH | MAX31856_FAULT_TCLOW, "out of range"},
      {MAX31856_FAULT_CJRANGE | MAX31856_FAULT_CJHIGH | MAX31856_FAULT_CJLOW, "board temperature"},
  };
  out[0] = '\0';
  for (const auto& k : KINDS) {
    if (bad & k.bits) {
      if (out[0]) strlcat(out, ", ", len);
      strlcat(out, k.name, len);
    }
  }
  if (!out[0]) strlcpy(out, "none", len);
}

}  // namespace thermo
