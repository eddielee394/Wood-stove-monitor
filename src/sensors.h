// Sensor task: reads both thermocouples once a second, smooths them,
// computes rate of change, debounces faults, and publishes to state.
#pragma once

namespace sensors {

void start();  // create the task (highest priority, core 1)

}  // namespace sensors
