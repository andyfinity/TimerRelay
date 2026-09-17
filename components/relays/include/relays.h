// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
// relays - low-level GPIO driver for the Seeed 6-channel relay board.
//
// Relay -> XIAO pin -> ESP32C6 GPIO (from the board schematic, 02 Power.kicad_sch):
//   Relay 1 (K1) : D2  -> GPIO2
//   Relay 2 (K2) : D3  -> GPIO21
//   Relay 3 (K3) : D1  -> GPIO1
//   Relay 4 (K4) : D0  -> GPIO0
//   Relay 5 (K5) : D8  -> GPIO19
//   Relay 6 (K6) : D10 -> GPIO18
//
// Each channel is an S9013 NPN low-side driver with a 10k base pulldown, so the
// drive is ACTIVE-HIGH: GPIO high -> coil energized -> COM connects to NO
// ("enabled"); GPIO low -> de-energized -> COM connects to NC ("disabled").
// A low output is therefore the safe/disabled default at boot.
#pragma once

#include <stdint.h>
#include "appstate.h"   // RELAY_COUNT

#ifdef __cplusplus
extern "C" {
#endif

// Configure all relay GPIOs as outputs driven LOW (all disabled / NC).
void relays_init(void);

// Apply the full 6-relay vector at once. `states[i]` != 0 energizes relay i.
// The six writes happen back-to-back (sub-microsecond apart), satisfying the
// "simultaneous within the same second" requirement.
void relays_apply(const uint8_t states[RELAY_COUNT]);

#ifdef __cplusplus
}
#endif
