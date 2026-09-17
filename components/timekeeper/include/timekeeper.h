// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
// timekeeper - system clock: SNTP over Wi-Fi, manual set fallback, and POSIX
// timezone handling (with DST) from a curated region list.
//
// This board has no battery-backed RTC, so on a cold boot the clock is invalid
// until NTP syncs or the user sets it manually. `time_valid` in runtime state
// gates schedule evaluation for AUTO relays.
#pragma once

#include <time.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void timekeeper_init(void);

// Apply a POSIX TZ string (e.g. "EST5EDT,M3.2.0,M11.1.0") to the environment.
void timekeeper_apply_tz(const char *posix_tz);

// Called by netmgr once the station has an IP; (re)starts SNTP.
void timekeeper_on_got_ip(void);

// Manually set the wall clock (UTC epoch). Marks the time valid + source=manual.
void timekeeper_set_manual(time_t utc_epoch);

// True once the clock is believed correct (NTP synced or manually set).
bool timekeeper_time_valid(void);

// Curated timezone list for the UI. Enumerate with count/at; look up POSIX by
// human name. Returns NULL if not found.
size_t      timekeeper_tz_count(void);
bool        timekeeper_tz_at(size_t idx, const char **name, const char **posix);
const char *timekeeper_tz_posix_for(const char *name);

#ifdef __cplusplus
}
#endif
