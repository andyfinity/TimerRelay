// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
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
// Only relay-target events contribute; macro-target events are handled as edges
// (see schedule_collect_macro_starts).
void schedule_eval(time_t now, bool auto_on[RELAY_COUNT]);

// Collect macro-target schedule events that fired in the window (since, now].
// Fills out[] with up to `max` macro indices (each such event fires at most
// once, at its most recent occurrence) and returns the count. Edge-triggered:
// the caller advances `since` to `now` between calls.
int schedule_collect_macro_starts(time_t now, time_t since, uint8_t *out, int max);

// Find the soonest enabled event occurring strictly after `now`. On success
// sets *out_epoch (local epoch of the next firing) and writes a short human
// description into desc[] (e.g. "R1,R2 on" or "Macro: Blink"). Returns false if
// no enabled event has any future occurrence.
bool schedule_next_event(time_t now, time_t *out_epoch, char *desc, size_t desc_len);

#ifdef __cplusplus
}
#endif
