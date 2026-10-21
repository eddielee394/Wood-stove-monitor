// Shared device state: written by the sensor and alarm tasks, read by the UI
// and (later) network tasks as whole-struct snapshots under a mutex.
#pragma once

#include <stdint.h>

#include "thermo.h"

namespace state {

// Ordered by severity; the buzzer plays the highest unsilenced level.
enum class Level : uint8_t { None, Reload, Warning, Fault, Critical };

struct Channel {
  bool valid;      // tempF holds a smoothed reading
  bool faulted;    // debounced probe fault (or chip missing)
  float tempF;     // smoothed temperature, NAN when !valid
  float rateFpm;   // deg F per minute, NAN until enough history
  float boardF;    // MAX31856 cold-junction temperature
  uint8_t fault;   // last raw fault register, 0 = OK
  bool present;    // chip answering on SPI
};

struct Alarms {
  Level temp[thermo::COUNT];   // None / Warning / Critical per probe
  bool fault[thermo::COUNT];   // probe fault or stale sensor data
  bool rateHigh;               // FLUE rate-of-rise
  bool reload;                 // STOVE cooled below the reload threshold
  bool reloadArmed;            // STOVE has been hot enough to arm reload
  bool ideal;                  // burn is in the "ideal flue" band (info only)
  Level highest;
  bool silenced;
  uint16_t silenceLeftS;       // seconds until a silenced alarm re-arms
  bool audible;                // buzzer is currently sounding a pattern
};

struct Snapshot {
  Channel ch[thermo::COUNT];
  uint32_t sensorMs;  // millis() of the last sensor update, 0 = none yet
  Alarms alarms;
};

void begin();
Snapshot get();
void setChannels(const Channel (&ch)[thermo::COUNT]);
void setAlarms(const Alarms& a);

// Silence/acknowledge, from any task. The alarm task consumes it.
void requestSilence();
bool takeSilenceRequest();

// Bench mode: probe-fault alarms stay visible but silent. RAM only, so it
// always clears on reboot; for working on the hardware with a probe unplugged.
void setBenchMuteFaults(bool on);
bool benchMuteFaults();

const char* levelName(Level l);
const char* resetReason();  // why the board last restarted
// Restart on purpose because internal RAM ran out (network stuck); shows
// as "low memory restart" after the reboot.
void restartLowMemory();
// Restart on purpose because Wi-Fi says connected but the router stopped
// answering, and reconnecting didn't help; shows as "network stuck restart".
void restartNetworkStuck();

}  // namespace state
