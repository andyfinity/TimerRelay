// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "macros.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "macros";

// A running macro is a small call stack of frames so a macro can call other
// macros; each frame also tracks how many passes it has made for looping.
typedef struct {
    int      macro_idx;
    int      step;   // next step index to execute in this macro
    uint16_t pass;   // completed passes over the step sequence
} frame_t;

static bool        s_active;
static macro_run_t s_run;
static frame_t     s_stack[MACRO_CALL_DEPTH];
static int         s_depth;
static int64_t     s_next_ms;   // monotonic due time of the current step

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool macro_valid_locked(int idx)
{
    if (idx < 0 || idx >= MAX_MACROS) return false;
    const macro_t *m = &appstate_config()->macros[idx];
    return m->used && m->step_count > 0;
}

// Reflect engine state into runtime for UI / REST / Companion. Reports the
// top-level (user-started) macro; sub-macro calls are an internal detail. Lock
// held.
static void publish_status_locked(void)
{
    app_runtime_t *rt = appstate_runtime();
    rt->macro_active = s_active;
    rt->macro_run = s_run;
    if (s_active && s_depth > 0) {
        const frame_t *root = &s_stack[0];
        const macro_t *m = &appstate_config()->macros[root->macro_idx];
        rt->macro_index = (int8_t)root->macro_idx;
        rt->macro_step = (uint8_t)(root->step > m->step_count ? m->step_count : root->step);
        rt->macro_steps_total = m->step_count;
        strlcpy(rt->macro_name, m->name, sizeof(rt->macro_name));
    } else {
        rt->macro_index = -1;
        rt->macro_step = 0;
        rt->macro_steps_total = 0;
        rt->macro_name[0] = '\0';
    }
}

// Position the stack so the top frame's `step` points at an executable step,
// unwinding finished sequences by looping (loop_count) or popping (return to
// caller). Returns false when the whole macro has finished (stack empty).
static bool resolve_current_locked(void)
{
    app_config_t *cfg = appstate_config();
    int guard = 0;
    while (s_depth > 0 && guard++ < MACRO_CALL_DEPTH * 4 + 8) {
        frame_t *f = &s_stack[s_depth - 1];
        const macro_t *m = &cfg->macros[f->macro_idx];
        if (!m->used) { s_depth--; continue; }        // macro vanished under us
        if (f->step < m->step_count) return true;      // executable step ready
        f->pass++;                                     // one pass completed
        if (m->loop_count == 0 || f->pass < m->loop_count) {
            f->step = 0;                               // loop again
        } else {
            s_depth--;                                 // pop, resume caller
        }
    }
    return s_depth > 0;
}

static uint16_t current_delay_locked(void)
{
    const frame_t *f = &s_stack[s_depth - 1];
    return appstate_config()->macros[f->macro_idx].steps[f->step].delay_s;
}

// Execute the current step and advance past it. A relay step applies its mode;
// a call step pushes a frame for the target macro. Returns true if a relay mode
// changed.
static bool execute_current_locked(void)
{
    app_config_t *cfg = appstate_config();
    frame_t *f = &s_stack[s_depth - 1];
    const macro_step_t *st = &cfg->macros[f->macro_idx].steps[f->step];
    f->step++;

    if (st->call_macro >= 0) {
        int ci = st->call_macro;
        if (ci < MAX_MACROS && cfg->macros[ci].used && cfg->macros[ci].step_count > 0 &&
            s_depth < MACRO_CALL_DEPTH) {
            s_stack[s_depth].macro_idx = ci;
            s_stack[s_depth].step = 0;
            s_stack[s_depth].pass = 0;
            s_depth++;
        } else {
            ESP_LOGW(TAG, "skipping call to macro %d (invalid or too deep)", ci);
        }
        return false;
    }

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

static void begin_locked(int idx, macro_run_t mode)
{
    s_active = true;
    s_run = mode;
    s_depth = 1;
    s_stack[0].macro_idx = idx;
    s_stack[0].step = 0;
    s_stack[0].pass = 0;
}

bool macros_start(int idx, macro_run_t mode)
{
    appstate_lock();
    if (!macro_valid_locked(idx)) { appstate_unlock(); return false; }
    begin_locked(idx, mode);
    if (!resolve_current_locked()) s_active = false;
    else s_next_ms = now_ms() + current_delay_locked();
    publish_status_locked();
    ESP_LOGI(TAG, "macro %d started (%s)", idx, mode == MACRO_RUN_AUTO ? "auto" : "manual");
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
        if (cfg->macros[i].used && strcmp(cfg->macros[i].name, name) == 0) { found = i; break; }
    }
    appstate_unlock();
    return found >= 0 ? macros_start(found, mode) : false;
}

void macros_stop(void)
{
    appstate_lock();
    if (s_active) { ESP_LOGI(TAG, "macro stopped"); s_active = false; s_depth = 0; }
    publish_status_locked();
    appstate_unlock();
}

bool macros_step(int idx)
{
    bool changed = false;
    appstate_lock();
    if (!s_active) {
        if (idx < 0 || !macro_valid_locked(idx)) { appstate_unlock(); return false; }
        begin_locked(idx, MACRO_RUN_MANUAL);
    }
    s_run = MACRO_RUN_MANUAL;
    if (resolve_current_locked()) {
        changed = execute_current_locked();
        if (resolve_current_locked()) s_next_ms = now_ms() + current_delay_locked();
        else s_active = false;
    } else {
        s_active = false;
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
        while (s_active && t >= s_next_ms && guard++ < 256) {
            if (!resolve_current_locked()) { s_active = false; break; }
            if (execute_current_locked()) changed = true;
            stepped = true;
            if (resolve_current_locked()) s_next_ms += current_delay_locked();
            else s_active = false;
        }
        if (stepped) publish_status_locked();
    }
    appstate_unlock();

    if (changed) appstate_save_config();
    return changed;
}
