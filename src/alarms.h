// Alarm task: owns every safety decision. Evaluates thresholds (with
// hysteresis), probe faults, stale data, flue rate-of-rise and the reload
// alert, handles silence/acknowledge, and drives the buzzer. Depends only on
// state, settings and the buzzer - never on the network or UI.
#pragma once

namespace alarms {

void start();  // create the task (high priority, core 1)

}  // namespace alarms
