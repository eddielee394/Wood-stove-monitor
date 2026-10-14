// Outside temperature from Open-Meteo (free, no API key), fetched over HTTPS
// every 15 minutes by its own low-priority task on core 0. Display only:
// nothing in the sensor or alarm path reads it. The location (latitude and
// longitude) is stored in NVS only, never in the source.
#pragma once

#include <stdint.h>

namespace weather {

enum class Sky : uint8_t { Unknown, Clear, Cloudy, Fog, Rain, Snow, Storm };

struct Status {
  bool located;        // a location is set
  bool valid;          // tempF is from a fetch less than an hour old
  float tempF;         // NAN when !valid
  Sky sky;
  bool isDay;
  uint32_t ageS;       // seconds since the last good fetch, 0 if none
  char sunrise[6];     // "HH:MM" local time, "" if unknown
  char sunset[6];
  char error[48];      // last fetch error, "" after a good fetch
  float lat, lon;
};

void start();
Status status();
const char* skyName(Sky s);

// Validated (-90..90, -180..180) and saved; triggers a fetch. clear() turns
// the feature off.
bool setLocation(float lat, float lon);
void clearLocation();

}  // namespace weather
