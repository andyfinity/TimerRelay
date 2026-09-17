// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "appstate.h"
#include "storage.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "appstate";

// NVS keys. The relay modes change often (every override), so they live in
// their own tiny blob; the settings blob (Wi-Fi, TZ, schedule) changes rarely.
#define KEY_SETTINGS "settings"
#define KEY_MODES    "modes"

// On-flash image of everything except the frequently-written relay modes.
typedef struct {
    uint32_t magic;
    char     wifi_ssid[WIFI_SSID_MAX];
    char     wifi_pass[WIFI_PASS_MAX];
    char     tz_name[TZ_NAME_MAX];
    char     tz_posix[TZ_POSIX_MAX];
    char     hostname[HOSTNAME_MAX];
    uint16_t event_count;
    sched_event_t events[MAX_SCHEDULE_EVENTS];
} settings_blob_t;

#define SETTINGS_MAGIC 0x54524C31u  // "TRL1"

static SemaphoreHandle_t s_mutex;
static app_config_t      s_cfg;
static app_runtime_t     s_rt;

// Shadows of what is currently on flash, for write-on-change comparison.
static settings_blob_t   s_shadow_settings;
static uint8_t           s_shadow_modes[RELAY_COUNT];

static void pack_settings(settings_blob_t *b)
{
    memset(b, 0, sizeof(*b));
    b->magic = SETTINGS_MAGIC;
    memcpy(b->wifi_ssid, s_cfg.wifi_ssid, sizeof(b->wifi_ssid));
    memcpy(b->wifi_pass, s_cfg.wifi_pass, sizeof(b->wifi_pass));
    memcpy(b->tz_name,   s_cfg.tz_name,   sizeof(b->tz_name));
    memcpy(b->tz_posix,  s_cfg.tz_posix,  sizeof(b->tz_posix));
    memcpy(b->hostname,  s_cfg.hostname,  sizeof(b->hostname));
    b->event_count = s_cfg.event_count;
    memcpy(b->events, s_cfg.events, sizeof(b->events));
}

static void unpack_settings(const settings_blob_t *b)
{
    memcpy(s_cfg.wifi_ssid, b->wifi_ssid, sizeof(s_cfg.wifi_ssid));
    memcpy(s_cfg.wifi_pass, b->wifi_pass, sizeof(s_cfg.wifi_pass));
    memcpy(s_cfg.tz_name,   b->tz_name,   sizeof(s_cfg.tz_name));
    memcpy(s_cfg.tz_posix,  b->tz_posix,  sizeof(s_cfg.tz_posix));
    memcpy(s_cfg.hostname,  b->hostname,  sizeof(s_cfg.hostname));
    s_cfg.event_count = b->event_count > MAX_SCHEDULE_EVENTS
                            ? MAX_SCHEDULE_EVENTS : b->event_count;
    memcpy(s_cfg.events, b->events, sizeof(s_cfg.events));
}

static void load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    // Sensible defaults for a fresh device.
    strlcpy(s_cfg.tz_name,  "UTC", sizeof(s_cfg.tz_name));
    strlcpy(s_cfg.tz_posix, "UTC0", sizeof(s_cfg.tz_posix));
    strlcpy(s_cfg.hostname, "timerrelay", sizeof(s_cfg.hostname));
    for (int i = 0; i < RELAY_COUNT; i++) {
        s_cfg.relay_mode[i] = RELAY_MODE_AUTO;
    }
    s_cfg.event_count = 0;
}

void appstate_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(s_mutex);

    load_defaults();
    memset(&s_rt, 0, sizeof(s_rt));

    settings_blob_t sb;
    if (storage_get_blob(KEY_SETTINGS, &sb, sizeof(sb)) && sb.magic == SETTINGS_MAGIC) {
        unpack_settings(&sb);
        ESP_LOGI(TAG, "loaded settings (%u events, ssid='%s', tz='%s')",
                 s_cfg.event_count, s_cfg.wifi_ssid, s_cfg.tz_name);
    } else {
        ESP_LOGW(TAG, "no valid settings in NVS; using defaults");
    }

    uint8_t modes[RELAY_COUNT];
    if (storage_get_blob(KEY_MODES, modes, sizeof(modes))) {
        for (int i = 0; i < RELAY_COUNT; i++) {
            s_cfg.relay_mode[i] = (modes[i] <= RELAY_MODE_OFF) ? modes[i] : RELAY_MODE_AUTO;
        }
    }

    // Prime shadows so the first save only writes if something actually changed.
    pack_settings(&s_shadow_settings);
    memcpy(s_shadow_modes, s_cfg.relay_mode, sizeof(s_shadow_modes));
}

void appstate_lock(void)   { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void appstate_unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

app_config_t  *appstate_config(void)  { return &s_cfg; }
app_runtime_t *appstate_runtime(void) { return &s_rt; }

void appstate_save_config(void)
{
    // Caller may or may not hold the lock; take it recursively to be safe.
    appstate_lock();

    settings_blob_t sb;
    pack_settings(&sb);
    if (memcmp(&sb, &s_shadow_settings, sizeof(sb)) != 0) {
        if (storage_set_blob(KEY_SETTINGS, &sb, sizeof(sb)) == ESP_OK) {
            memcpy(&s_shadow_settings, &sb, sizeof(sb));
            ESP_LOGI(TAG, "settings persisted");
        }
    }

    if (memcmp(s_cfg.relay_mode, s_shadow_modes, sizeof(s_shadow_modes)) != 0) {
        if (storage_set_blob(KEY_MODES, s_cfg.relay_mode, sizeof(s_shadow_modes)) == ESP_OK) {
            memcpy(s_shadow_modes, s_cfg.relay_mode, sizeof(s_shadow_modes));
            ESP_LOGI(TAG, "relay modes persisted");
        }
    }

    appstate_unlock();
}

relay_report_t appstate_report_for(relay_mode_t mode, bool auto_desired_on)
{
    switch (mode) {
        case RELAY_MODE_ON:  return RELAY_REPORT_MANUAL_ON;
        case RELAY_MODE_OFF: return RELAY_REPORT_MANUAL_OFF;
        case RELAY_MODE_AUTO:
        default:
            return auto_desired_on ? RELAY_REPORT_AUTO_ON : RELAY_REPORT_AUTO_OFF;
    }
}
