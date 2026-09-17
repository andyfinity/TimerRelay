// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
#include "schedule.h"

#include <string.h>

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
        if (!e->enabled || e->relay_mask == 0) continue;

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
