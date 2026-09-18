// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "schedule.h"

#include <string.h>
#include <stdio.h>

// Build a local-time epoch from calendar fields, letting mktime resolve DST.
// Returns (time_t)-1 on an impossible date (e.g. Feb 30). `out_wday` receives
// the normalized weekday (0=Sun) when non-NULL.
static time_t make_local(int year, int mon1, int day, int h, int m, int s,
                         int *out_wday, bool *day_ok)
{
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon1 - 1;
    tm.tm_mday = day;
    tm.tm_hour = h;
    tm.tm_min  = m;
    tm.tm_sec  = s;
    tm.tm_isdst = -1;
    int req_mday = day, req_mon = mon1 - 1;
    time_t ts = mktime(&tm);
    if (out_wday) *out_wday = tm.tm_wday;
    if (day_ok)   *day_ok = (tm.tm_mday == req_mday && tm.tm_mon == req_mon);
    return ts;
}

// Most-recent occurrence of one event at or before `now`. Returns false if the
// event has no occurrence <= now. On success sets *ts.
static bool last_occurrence(const sched_event_t *e, time_t now,
                            const struct tm *lt, time_t *ts)
{
    const int h = e->hour, m = e->minute, s = e->second;

    switch (e->type) {
    case SCHED_WEEKLY: {
        // Walk back up to 7 days; first matching weekday with cand <= now wins.
        for (int back = 0; back <= 7; back++) {
            int wday; bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1,
                                     lt->tm_mday - back, h, m, s, &wday, &ok);
            if (cand == (time_t)-1 || !ok) continue;
            if (!(e->dow_mask & (1u << wday))) continue;
            if (cand <= now) { *ts = cand; return true; }
        }
        return false;
    }
    case SCHED_DAILY: {
        for (int back = 0; back <= 1; back++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1,
                                     lt->tm_mday - back, h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand <= now) { *ts = cand; return true; }
        }
        return false;
    }
    case SCHED_MONTHLY: {
        for (int back = 0; back <= 2; back++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1 - back,
                                     e->day, h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand <= now) { *ts = cand; return true; }
        }
        return false;
    }
    case SCHED_YEARLY: {
        for (int back = 0; back <= 1; back++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900 - back, e->month,
                                     e->day, h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand <= now) { *ts = cand; return true; }
        }
        return false;
    }
    case SCHED_ONESHOT: {
        bool ok;
        time_t cand = make_local(e->year, e->month, e->day, h, m, s, NULL, &ok);
        if (cand != (time_t)-1 && ok && cand <= now) { *ts = cand; return true; }
        return false;
    }
    default:
        return false;
    }
}

void schedule_eval(time_t now, bool auto_on[RELAY_COUNT])
{
    struct tm lt;
    localtime_r(&now, &lt);

    // Default: disabled unless an event says otherwise.
    time_t best_ts[RELAY_COUNT];
    for (int r = 0; r < RELAY_COUNT; r++) {
        auto_on[r] = false;
        best_ts[r] = (time_t)-1;
    }

    appstate_lock();
    app_config_t *cfg = appstate_config();
    uint16_t n = cfg->event_count;
    if (n > MAX_SCHEDULE_EVENTS) n = MAX_SCHEDULE_EVENTS;

    for (uint16_t i = 0; i < n; i++) {
        const sched_event_t *e = &cfg->events[i];
        if (!e->enabled) continue;
        if (e->target == SCHED_TARGET_MACRO) continue;   // edges, not levels
        if (e->relay_mask == 0) continue;

        time_t ts;
        if (!last_occurrence(e, now, &lt, &ts)) continue;

        for (int r = 0; r < RELAY_COUNT; r++) {
            if (!(e->relay_mask & (1u << r))) continue;
            // >= so that a later-defined event wins an exact-second tie,
            // giving deterministic behaviour for same-second transitions.
            if (best_ts[r] == (time_t)-1 || ts >= best_ts[r]) {
                best_ts[r] = ts;
                auto_on[r] = (e->action != 0);
            }
        }
    }
    appstate_unlock();
}

int schedule_collect_macro_starts(time_t now, time_t since, uint8_t *out, int max)
{
    struct tm lt;
    localtime_r(&now, &lt);

    int count = 0;
    appstate_lock();
    app_config_t *cfg = appstate_config();
    uint16_t n = cfg->event_count;
    if (n > MAX_SCHEDULE_EVENTS) n = MAX_SCHEDULE_EVENTS;

    for (uint16_t i = 0; i < n && count < max; i++) {
        const sched_event_t *e = &cfg->events[i];
        if (!e->enabled || e->target != SCHED_TARGET_MACRO) continue;

        time_t ts;
        if (!last_occurrence(e, now, &lt, &ts)) continue;
        if (ts > since && ts <= now) {
            out[count++] = e->macro_idx;
        }
    }
    appstate_unlock();
    return count;
}

// Soonest occurrence of one event strictly after `now`, mirroring
// last_occurrence but searching forward.
static bool next_occurrence(const sched_event_t *e, time_t now,
                            const struct tm *lt, time_t *ts)
{
    const int h = e->hour, m = e->minute, s = e->second;

    switch (e->type) {
    case SCHED_WEEKLY:
        for (int fwd = 0; fwd <= 7; fwd++) {
            int wday; bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1,
                                     lt->tm_mday + fwd, h, m, s, &wday, &ok);
            if (cand == (time_t)-1 || !ok) continue;
            if (!(e->dow_mask & (1u << wday))) continue;
            if (cand > now) { *ts = cand; return true; }
        }
        return false;
    case SCHED_DAILY:
        for (int fwd = 0; fwd <= 1; fwd++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1,
                                     lt->tm_mday + fwd, h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand > now) { *ts = cand; return true; }
        }
        return false;
    case SCHED_MONTHLY:
        for (int fwd = 0; fwd <= 3; fwd++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900, lt->tm_mon + 1 + fwd,
                                     e->day, h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand > now) { *ts = cand; return true; }
        }
        return false;
    case SCHED_YEARLY:
        for (int fwd = 0; fwd <= 1; fwd++) {
            bool ok;
            time_t cand = make_local(lt->tm_year + 1900 + fwd, e->month, e->day,
                                     h, m, s, NULL, &ok);
            if (cand != (time_t)-1 && ok && cand > now) { *ts = cand; return true; }
        }
        return false;
    case SCHED_ONESHOT: {
        bool ok;
        time_t cand = make_local(e->year, e->month, e->day, h, m, s, NULL, &ok);
        if (cand != (time_t)-1 && ok && cand > now) { *ts = cand; return true; }
        return false;
    }
    default:
        return false;
    }
}

bool schedule_next_event(time_t now, time_t *out_epoch, char *desc, size_t desc_len)
{
    struct tm lt;
    localtime_r(&now, &lt);

    time_t best = (time_t)-1;
    int best_i = -1;

    appstate_lock();
    app_config_t *cfg = appstate_config();
    uint16_t n = cfg->event_count;
    if (n > MAX_SCHEDULE_EVENTS) n = MAX_SCHEDULE_EVENTS;

    for (uint16_t i = 0; i < n; i++) {
        const sched_event_t *e = &cfg->events[i];
        if (!e->enabled) continue;
        if (e->target == SCHED_TARGET_RELAYS && e->relay_mask == 0) continue;
        time_t ts;
        if (!next_occurrence(e, now, &lt, &ts)) continue;
        if (best == (time_t)-1 || ts < best) { best = ts; best_i = i; }
    }

    if (best_i >= 0 && desc && desc_len) {
        const sched_event_t *e = &cfg->events[best_i];
        if (e->target == SCHED_TARGET_MACRO) {
            int mi = e->macro_idx;
            const char *nm = (mi >= 0 && mi < MAX_MACROS && cfg->macros[mi].used)
                                 ? cfg->macros[mi].name : "?";
            snprintf(desc, desc_len, "Macro: %s", nm);
        } else {
            char rl[24];
            int p = 0;
            rl[0] = '\0';
            for (int r = 0; r < RELAY_COUNT; r++)
                if (e->relay_mask & (1u << r))
                    p += snprintf(rl + p, sizeof(rl) - p, "%sR%d", p ? "," : "", r + 1);
            snprintf(desc, desc_len, "%s %s", rl, e->action ? "on" : "off");
        }
    }
    appstate_unlock();

    if (best_i < 0) return false;
    if (out_epoch) *out_epoch = best;
    return true;
}
