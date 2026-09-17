// schedule - evaluates the configured events into a desired auto-state vector.
//
// Model: each event is a *transition* that, when it fires, sets its target
// relays to `action`. A relay's auto state at time `now` is the action of the
// most recently fired applicable event (across all recurrence types). If no
// event has ever applied, the relay is disabled (off) - matching the safe
// default. Because the whole vector is recomputed and returned together, relays
// scheduled to change in the same second change together.
#pragma once

#include <time.h>
#include <stdbool.h>
#include "appstate.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compute the desired auto (schedule-driven) on/off state for every relay at
// local epoch time `now`. Requires the TZ environment to be set (timekeeper
// does this) so recurrence math is DST-correct. Reads config under the lock.
void schedule_eval(time_t now, bool auto_on[RELAY_COUNT]);

#ifdef __cplusplus
}
#endif
