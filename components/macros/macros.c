// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "macros.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "macros";

// Engine state (guarded by the appstate lock, which we take whenever touching
// config/runtime; the engine fields themselves are only mutated there too).
static bool        s_active;
static int         s_idx;          // active macro index
static macro_run_t s_run;
static int         s_next_step;    // index of the next step to execute
static int64_t     s_next_ms;      // monotonic time the next step is due (AUTO)

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// Reflect engine state into runtime for UI / REST / Companion. Lock held.
static void publish_status_locked(void)
{
    app_runtime_t *rt = appstate_runtime();
    rt->macro_active = s_active;
    rt->macro_run = s_run;
    if (s_active) {
        rt->macro_index = (int8_t)s_idx;
        rt->macro_step = (uint8_t)s_next_step;   // steps completed so far
        const macro_t *m = &appstate_config()->macros[s_idx];
        rt->macro_steps_total = m->step_count;
        strlcpy(rt->macro_name, m->name, sizeof(rt->macro_name));
    } else {
        rt->macro_index = -1;
        rt->macro_step = 0;
        rt->macro_steps_total = 0;
        rt->macro_name[0] = '\0';
    }
}

static bool macro_valid_locked(int idx)
{
    if (idx < 0 || idx >= MAX_MACROS) return false;
    const macro_t *m = &appstate_config()->macros[idx];
    return m->used && m->step_count > 0;
}

// Apply one step's action to its relays (as override modes). Lock held.
// Returns true if any mode actually changed.
static bool apply_step_locked(int stepno)
{
    app_config_t *cfg = appstate_config();
    const macro_t *m = &cfg->macros[s_idx];
    if (stepno < 0 || stepno >= m->step_count) return false;
    const macro_step_t *st = &m->steps[stepno];
    uint8_t mode = st->action <= RELAY_MODE_OFF ? st->action : RELAY_MODE_AUTO;
    bool changed = false;
    for (int r = 0; r < RELAY_COUNT; r++) {
        if ((st->relay_mask & (1u << r)) && cfg->relay_mode[r] != mode) {
            cfg->relay_mode[r] = mode;
            changed = true;
        }
    }
    return changed;
}

// Execute the current pending step and advance. Lock held. Sets *changed.
static void step_once_locked(bool *changed)
{
    const macro_t *m = &appstate_config()->macros[s_idx];
    if (apply_step_locked(s_next_step)) *changed = true;
    s_next_step++;
    if (s_next_step >= m->step_count) {
        ESP_LOGI(TAG, "macro %d ('%s') complete", s_idx, m->name);
        s_active = false;
    } else {
        // Schedule from the due time, not "now", to avoid drift across steps.
        s_next_ms += (int64_t)m->steps[s_next_step].delay_s * 1000;
    }
}

bool macros_start(int idx, macro_run_t mode)
{
    appstate_lock();
    if (!macro_valid_locked(idx)) { appstate_unlock(); return false; }
    const macro_t *m = &appstate_config()->macros[idx];
    s_active = true;
    s_idx = idx;
    s_run = mode;
    s_next_step = 0;
    s_next_ms = now_ms() + (int64_t)m->steps[0].delay_s * 1000;
    publish_status_locked();
    ESP_LOGI(TAG, "macro %d ('%s') started (%s)", idx, m->name,
             mode == MACRO_RUN_AUTO ? "auto" : "manual");
    appstate_unlock();
    return true;
}

bool macros_start_by_name(const char *name, macro_run_t mode)
{
    if (!name || !name[0]) return false;
    int found = -1;
    appstate_lock();
    app_config_t *cfg = appstate_config();
    for (int i = 0; i < MAX_MACROS; i++) {
        if (cfg->macros[i].used && strcmp(cfg->macros[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    appstate_unlock();
    return found >= 0 ? macros_start(found, mode) : false;
}

void macros_stop(void)
{
    appstate_lock();
    if (s_active) {
        ESP_LOGI(TAG, "macro %d stopped", s_idx);
        s_active = false;
    }
    publish_status_locked();
    appstate_unlock();
}

bool macros_step(int idx)
{
    bool changed = false;
    appstate_lock();
    if (!s_active) {
        if (idx < 0 || !macro_valid_locked(idx)) { appstate_unlock(); return false; }
        s_active = true;
        s_idx = idx;
        s_next_step = 0;
        s_next_ms = now_ms();
    }
    s_run = MACRO_RUN_MANUAL;      // stepping implies manual hold
    if (s_active) {
        step_once_locked(&changed);
    }
    publish_status_locked();
    appstate_unlock();

    if (changed) appstate_save_config();
    return true;
}

bool macros_tick(void)
{
    bool changed = false, stepped = false;
    appstate_lock();
    if (s_active && s_run == MACRO_RUN_AUTO) {
        int64_t t = now_ms();
        int guard = 0;
        while (s_active && t >= s_next_ms && guard++ < MAX_MACRO_STEPS + 1) {
            step_once_locked(&changed);
            stepped = true;
        }
        if (stepped) publish_status_locked();
    }
    appstate_unlock();

    if (changed) appstate_save_config();
    return changed;
}
