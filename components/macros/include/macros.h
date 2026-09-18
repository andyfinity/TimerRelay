// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// macros - runs a prearranged, time-separated sequence of relay actions.
//
// A macro manipulates relay OVERRIDE MODES only; the controller task remains the
// sole writer of the physical outputs. Step timing is monotonic (esp_timer), so
// macros run correctly even before the wall clock is valid and are unaffected by
// NTP time jumps. Only one macro is active at a time; starting another replaces
// it. Mode changes a macro makes are persisted like any manual override.
#pragma once

#include <stdbool.h>
#include "appstate.h"   // macro_run_t

#ifdef __cplusplus
extern "C" {
#endif

// Start macro `idx` in the given run mode. Returns false for a bad/empty macro.
bool macros_start(int idx, macro_run_t mode);

// Start a macro by name (case-sensitive). Returns false if not found/empty.
bool macros_start_by_name(const char *name, macro_run_t mode);

// Stop the active macro. Relays keep whatever state the macro last set.
void macros_stop(void);

// Advance one step now. If a macro is active, it is single-stepped (and switched
// to MANUAL so it holds afterwards). If none is active and idx >= 0, that macro
// is started in MANUAL mode and its first step is executed. idx < 0 with no
// active macro is a no-op. Returns true if a step was executed.
bool macros_step(int idx);

// Called from the controller each tick. Advances an AUTO-mode macro whose next
// step(s) are due. Returns true if it changed any relay mode this call.
bool macros_tick(void);

#ifdef __cplusplus
}
#endif
