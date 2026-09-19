// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// appstate - central, mutex-protected configuration and runtime state model.
//
// Every other component reads and writes the device state through this module so
// there is a single source of truth. Persisted fields are saved to NVS (via the
// storage component) only when they actually change, to keep flash wear low.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RELAY_COUNT          6
#define MAX_SCHEDULE_EVENTS  64
#define MAX_MACROS           8
#define MAX_MACRO_STEPS      24
#define MACRO_NAME_MAX       24
#define MACRO_CALL_DEPTH     8    // max nested macro-call depth (recursion guard)
#define WIFI_SSID_MAX        33   // 32 + NUL
#define WIFI_PASS_MAX        64   // 63 + NUL
#define TZ_NAME_MAX          40   // human-selectable region key
#define TZ_POSIX_MAX         48   // POSIX TZ string with DST rules
#define HOSTNAME_MAX         33

// --- Relay override mode (per priority layer) ------------------------------
// The same three-valued mode is used by each override layer (manual, macro).
// AUTO means "release control to the next layer down": manual AUTO falls
// through to the macro layer, macro AUTO falls through to the schedule.
typedef enum {
    RELAY_MODE_AUTO = 0,   // release to the layer below (schedule for macros)
    RELAY_MODE_ON   = 1,   // override: forced enabled (energized / NO)
    RELAY_MODE_OFF  = 2,   // override: forced disabled (de-energized / NC)
} relay_mode_t;

// Reported state exposed to the UI / REST / Companion feedback. Names the layer
// that actually decides the relay's level (highest-priority non-AUTO layer wins;
// otherwise the schedule).
typedef enum {
    RELAY_REPORT_AUTO_OFF   = 0,
    RELAY_REPORT_AUTO_ON    = 1,
    RELAY_REPORT_MANUAL_OFF = 2,
    RELAY_REPORT_MANUAL_ON  = 3,
    RELAY_REPORT_MACRO_OFF  = 4,
    RELAY_REPORT_MACRO_ON   = 5,
} relay_report_t;

// --- Schedule event model --------------------------------------------------
// Recurrence type. WEEKLY is exposed in the UI today; the evaluator implements
// all of them so daily/monthly/yearly/one-time can be surfaced later with no
// firmware change to the core.
typedef enum {
    SCHED_WEEKLY  = 0,   // uses dow_mask + hh:mm:ss
    SCHED_DAILY   = 1,   // uses hh:mm:ss
    SCHED_MONTHLY = 2,   // uses day + hh:mm:ss
    SCHED_YEARLY  = 3,   // uses month + day + hh:mm:ss
    SCHED_ONESHOT = 4,   // uses year + month + day + hh:mm:ss (absolute)
} sched_type_t;

// What a schedule event acts on when it fires.
typedef enum {
    SCHED_TARGET_RELAYS = 0,  // level-based: drive relay_mask to `action`
    SCHED_TARGET_MACRO  = 1,  // edge-triggered: start macro `macro_idx` once
} sched_target_t;

// A schedule event is a *transition*. For SCHED_TARGET_RELAYS it sets the target
// relays to `action` (1 = enabled/NO, 0 = disabled/NC); a relay's auto state is
// decided by the most recently fired applicable event (see schedule.c). For
// SCHED_TARGET_MACRO it starts macro `macro_idx` at the scheduled second (an
// edge, fired once as local time crosses the event time).
typedef struct {
    bool     enabled;
    uint8_t  relay_mask;   // bit0..bit5 -> relay 1..6 this event applies to
    uint8_t  type;         // sched_type_t
    uint8_t  action;       // 1 = enable (NO/on), 0 = disable (NC/off)
    uint8_t  dow_mask;     // weekly: bit0=Sun .. bit6=Sat
    uint8_t  day;          // monthly/yearly/oneshot: 1..31
    uint8_t  month;        // yearly/oneshot: 1..12
    uint8_t  hour;         // 0..23
    uint8_t  minute;       // 0..59
    uint8_t  second;       // 0..59
    int16_t  year;         // oneshot: full year (e.g. 2026)
    uint8_t  target;       // sched_target_t
    uint8_t  macro_idx;    // when target == SCHED_TARGET_MACRO
} sched_event_t;

// --- Macro model -----------------------------------------------------------
// A macro is a named, ordered list of steps. Each step waits delay_s seconds
// (relative to the previous step) and then either applies `action` to its
// relay_mask (call_macro < 0) or runs another macro inline (call_macro >= 0).
// A macro's whole step sequence repeats `loop_count` times (0 = forever).
// Macros manipulate relay override modes only - the controller remains the sole
// writer of the physical outputs. Step timing is monotonic, so macros run even
// when the wall clock is not yet valid.
typedef struct {
    uint16_t delay_s;      // seconds to wait before this step (>= 0)
    uint8_t  relay_mask;   // bit0..bit5 -> relay 1..6 (when call_macro < 0)
    uint8_t  action;       // relay_mode_t applied to the masked relays' macro
                           // layer (AUTO releases them back to the schedule)
    int8_t   call_macro;   // -1 = relay action; >=0 = run that macro inline
} macro_step_t;

typedef struct {
    bool         used;
    char         name[MACRO_NAME_MAX];
    uint16_t     loop_count;   // number of passes over the steps; 0 = infinite
    uint8_t      step_count;
    macro_step_t steps[MAX_MACRO_STEPS];
} macro_t;

// How an active macro advances between steps.
typedef enum {
    MACRO_RUN_AUTO   = 0,  // advance automatically as each step's delay elapses
    MACRO_RUN_MANUAL = 1,  // hold at each step until an explicit "step" command
} macro_run_t;

// --- Time source -----------------------------------------------------------
typedef enum {
    TIME_SRC_NONE   = 0,   // no valid time yet
    TIME_SRC_NTP    = 1,
    TIME_SRC_MANUAL = 2,
} time_source_t;

// --- Wi-Fi / network status ------------------------------------------------
typedef enum {
    NET_STATE_BOOT = 0,
    NET_STATE_STA_CONNECTING,
    NET_STATE_STA_CONNECTED,
    NET_STATE_AP_FALLBACK,   // hosting our own config network
} net_state_t;

// --- Persisted configuration ----------------------------------------------
typedef struct {
    char     wifi_ssid[WIFI_SSID_MAX];
    char     wifi_pass[WIFI_PASS_MAX];
    char     tz_name[TZ_NAME_MAX];     // human key, e.g. "America/New_York"
    char     tz_posix[TZ_POSIX_MAX];   // POSIX TZ, e.g. "EST5EDT,M3.2.0,M11.1.0"
    char     hostname[HOSTNAME_MAX];
    // Two independent override layers, highest priority first. A relay's level
    // is decided by the highest layer set to ON/OFF; AUTO releases to the next
    // layer down (macro), and macro AUTO releases to the schedule. Manual is set
    // from the relay controls; macro is set by the macro engine.
    uint8_t  relay_manual[RELAY_COUNT];  // relay_mode_t per relay (top priority)
    uint8_t  relay_macro[RELAY_COUNT];   // relay_mode_t per relay (over schedule)
    uint16_t event_count;
    sched_event_t events[MAX_SCHEDULE_EVENTS];
    uint8_t  macro_count;
    macro_t  macros[MAX_MACROS];
} app_config_t;

// --- Volatile runtime state -----------------------------------------------
typedef struct {
    bool           time_valid;
    time_source_t  time_source;
    bool           ntp_reachable;      // last SNTP sync succeeded
    net_state_t    net_state;
    char           ip_addr[16];
    char           ap_ssid[WIFI_SSID_MAX];
    uint8_t        relay_physical[RELAY_COUNT];  // 1 = energized
    uint8_t        relay_report[RELAY_COUNT];    // relay_report_t
    // Active-macro status (for UI / REST / Companion feedback).
    bool           macro_active;
    int8_t         macro_index;                  // -1 when none active
    uint8_t        macro_run;                    // macro_run_t
    uint8_t        macro_step;                   // steps completed so far
    uint8_t        macro_steps_total;            // steps in the active macro
    char           macro_name[MACRO_NAME_MAX];
} app_runtime_t;

// Initialise the mutex and load persisted config (call once, early).
void appstate_init(void);

// Scoped locking. Always pair lock/unlock; keep critical sections short.
void appstate_lock(void);
void appstate_unlock(void);

// Direct pointers - only valid while the lock is held.
app_config_t  *appstate_config(void);
app_runtime_t *appstate_runtime(void);

// Persist the current config to NVS if it differs from what is stored.
// Safe to call often; it is a no-op when nothing changed (write-on-change).
void appstate_save_config(void);

// Resolve the override layers (manual > macro > schedule) for one relay into
// the desired physical level and its reported state. `manual` and `macro` are
// the per-relay override modes; `auto_desired_on` is what the schedule wants
// (already gated by clock validity by the caller). Writes *desired_on (may be
// NULL) and returns the report naming the deciding layer.
relay_report_t appstate_resolve(relay_mode_t manual, relay_mode_t macro,
                                bool auto_desired_on, bool *desired_on);

#ifdef __cplusplus
}
#endif
