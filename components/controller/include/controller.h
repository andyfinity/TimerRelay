// controller - the single 1 Hz control loop that owns relay output.
//
// Each tick it computes every relay's desired state from its override mode and
// (when the clock is valid) the schedule, updates the reported states used for
// feedback, and applies all six outputs together. Keeping this in one place is
// what guarantees relays scheduled for the same second switch simultaneously.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the control task. Call after relays/appstate/timekeeper are ready.
void controller_start(void);

// Wake the loop to re-evaluate immediately (e.g. after an override, schedule
// edit, or manual time set) instead of waiting for the next 1 s tick.
void controller_notify(void);

#ifdef __cplusplus
}
#endif
