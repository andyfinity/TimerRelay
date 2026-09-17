// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
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
#define WIFI_SSID_MAX        33   // 32 + NUL
#define WIFI_PASS_MAX        64   // 63 + NUL
#define TZ_NAME_MAX          40   // human-selectable region key
#define TZ_POSIX_MAX         48   // POSIX TZ string with DST rules
#define HOSTNAME_MAX         33

// --- Relay override mode (persisted per relay) -----------------------------
typedef enum {
    RELAY_MODE_AUTO = 0,   // follow the schedule
    RELAY_MODE_ON   = 1,   // manual override: forced enabled (energized / NO)
    RELAY_MODE_OFF  = 2,   // manual override: forced disabled (de-energized / NC)
} relay_mode_t;

// Reported state exposed to the UI / REST / Companion feedback.
typedef enum {
    RELAY_REPORT_AUTO_OFF = 0,
    RELAY_REPORT_AUTO_ON  = 1,
    RELAY_REPORT_MANUAL_OFF = 2,
    RELAY_REPORT_MANUAL_ON  = 3,
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

// A schedule event is a *transition*: at its time it sets the target relays to
// `action` (1 = enabled/NO, 0 = disabled/NC). A relay's auto state is decided by
// the most recently fired applicable event (see schedule.c).
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
} sched_event_t;

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
    uint8_t  relay_mode[RELAY_COUNT];  // relay_mode_t per relay
    uint16_t event_count;
    sched_event_t events[MAX_SCHEDULE_EVENTS];
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

// Compute the reported state for a relay from its mode + desired auto state.
relay_report_t appstate_report_for(relay_mode_t mode, bool auto_desired_on);

#ifdef __cplusplus
}
#endif
