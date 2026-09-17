// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
#include "timekeeper.h"
#include "appstate.h"

#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "timekeeper";
static bool s_sntp_started = false;

// A clock reading is "valid" once it is at/after this instant. 2024-01-01 UTC.
#define VALID_EPOCH_THRESHOLD 1704067200

static void mark_valid(time_source_t src)
{
    appstate_lock();
    app_runtime_t *rt = appstate_runtime();
    rt->time_valid = true;
    rt->time_source = src;
    if (src == TIME_SRC_NTP) rt->ntp_reachable = true;
    appstate_unlock();
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    time_t now = time(NULL);
    if (now >= VALID_EPOCH_THRESHOLD) {
        mark_valid(TIME_SRC_NTP);
        struct tm lt; localtime_r(&now, &lt);
        char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        ESP_LOGI(TAG, "SNTP sync: %s (local)", buf);
    }
}

void timekeeper_apply_tz(const char *posix_tz)
{
    if (!posix_tz || !posix_tz[0]) posix_tz = "UTC0";
    setenv("TZ", posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to POSIX '%s'", posix_tz);
}

void timekeeper_init(void)
{
    // Apply the stored timezone up front so any timestamps read as local.
    appstate_lock();
    char posix[TZ_POSIX_MAX];
    strlcpy(posix, appstate_config()->tz_posix, sizeof(posix));
    appstate_unlock();
    timekeeper_apply_tz(posix);

    // If the RTC (kept alive only across resets, not power loss) already holds a
    // plausible time, treat it as valid so scheduling can run immediately.
    time_t now = time(NULL);
    if (now >= VALID_EPOCH_THRESHOLD) {
        mark_valid(TIME_SRC_MANUAL);
        ESP_LOGI(TAG, "clock already valid at boot");
    } else {
        ESP_LOGW(TAG, "clock invalid at boot; awaiting NTP or manual set");
    }
}

void timekeeper_on_got_ip(void)
{
    if (s_sntp_started) {
        esp_netif_sntp_start();
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
    cfg.server_from_dhcp = true;      // honour DHCP-provided NTP if offered
    cfg.sync_cb = sntp_sync_cb;
    cfg.renew_servers_after_new_IP = true;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        s_sntp_started = true;
        sntp_set_sync_interval(3600 * 1000);   // resync hourly
        ESP_LOGI(TAG, "SNTP started");
    } else {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(err));
    }
}

void timekeeper_set_manual(time_t utc_epoch)
{
    struct timeval tv = { .tv_sec = utc_epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    mark_valid(TIME_SRC_MANUAL);
    ESP_LOGI(TAG, "clock set manually to epoch %lld", (long long)utc_epoch);
}

bool timekeeper_time_valid(void)
{
    appstate_lock();
    bool v = appstate_runtime()->time_valid;
    appstate_unlock();
    return v;
}
