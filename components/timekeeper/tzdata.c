// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
// Curated list of timezones as POSIX TZ strings (DST rules embedded). This is
// the standard embedded approach: small flash footprint, DST-correct, no need
// to ship the full IANA database. Add rows here to widen coverage.
#include "timekeeper.h"
#include <string.h>

typedef struct { const char *name; const char *posix; } tz_row_t;

static const tz_row_t TZ[] = {
    { "UTC",                    "UTC0" },
    // Americas
    { "America/New_York",       "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Chicago",        "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Denver",         "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Phoenix",        "MST7" },
    { "America/Los_Angeles",    "PST8PDT,M3.2.0,M11.1.0" },
    { "America/Anchorage",      "AKST9AKDT,M3.2.0,M11.1.0" },
    { "America/Halifax",        "AST4ADT,M3.2.0,M11.1.0" },
    { "America/Sao_Paulo",      "<-03>3" },
    { "America/Mexico_City",    "CST6" },
    // Europe / Africa
    { "Europe/London",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Berlin",          "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Paris",           "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Madrid",          "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Athens",          "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Moscow",          "MSK-3" },
    { "Africa/Johannesburg",    "SAST-2" },
    { "Africa/Cairo",           "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    // Asia / Middle East
    { "Asia/Dubai",             "<+04>-4" },
    { "Asia/Kolkata",           "IST-5:30" },
    { "Asia/Bangkok",           "<+07>-7" },
    { "Asia/Shanghai",          "CST-8" },
    { "Asia/Singapore",         "<+08>-8" },
    { "Asia/Tokyo",             "JST-9" },
    { "Asia/Seoul",             "KST-9" },
    { "Asia/Jerusalem",         "IST-2IDT,M3.4.4/26,M10.5.0" },
    // Oceania
    { "Australia/Perth",        "AWST-8" },
    { "Australia/Adelaide",     "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia/Sydney",       "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Pacific/Auckland",       "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Pacific/Honolulu",       "HST10" },
};

#define TZ_N (sizeof(TZ) / sizeof(TZ[0]))

size_t timekeeper_tz_count(void) { return TZ_N; }

bool timekeeper_tz_at(size_t idx, const char **name, const char **posix)
{
    if (idx >= TZ_N) return false;
    if (name)  *name  = TZ[idx].name;
    if (posix) *posix = TZ[idx].posix;
    return true;
}

const char *timekeeper_tz_posix_for(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < TZ_N; i++) {
        if (strcmp(name, TZ[i].name) == 0) return TZ[i].posix;
    }
    return NULL;
}
