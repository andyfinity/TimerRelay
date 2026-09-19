// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "controller.h"
#include "appstate.h"
#include "relays.h"
#include "schedule.h"
#include "timekeeper.h"
#include "macros.h"

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "controller";
static TaskHandle_t s_task;

// Wall-clock watermark for edge-triggering macro-start schedule events.
static time_t s_macro_since;
static bool   s_macro_since_init;

static void evaluate_and_apply(void)
{
    time_t now = time(NULL);
    bool time_valid = timekeeper_time_valid();

    // 1. Fire any macro-start schedule events crossing their time this tick.
    //    Re-sync the watermark (without firing) on first run or a clock jump so
    //    a backlog of past events doesn't all fire at boot / after NTP sync.
    if (time_valid) {
        if (!s_macro_since_init || now < s_macro_since || (now - s_macro_since) > 3) {
            s_macro_since = now;
            s_macro_since_init = true;
        } else {
            uint8_t starts[MAX_SCHEDULE_EVENTS];
            int nc = schedule_collect_macro_starts(now, s_macro_since, starts,
                                                   sizeof(starts));
            for (int i = 0; i < nc; i++) {
                macros_start(starts[i], MACRO_RUN_AUTO);
            }
            s_macro_since = now;
        }
    }

    // 2. Advance the active macro (monotonic timing; runs even without a clock).
    macros_tick();

    // 3. Relay levels: resolve the priority layers manual > macro > schedule.
    //    When time is invalid, auto_on stays all-false so schedule-controlled
    //    relays hold the safe disabled state until the clock is known; manual
    //    and macro overrides (monotonic-timed) still apply.
    bool auto_on[RELAY_COUNT] = {0};
    if (time_valid) {
        schedule_eval(now, auto_on);
    }

    uint8_t phys[RELAY_COUNT];
    appstate_lock();
    app_config_t  *cfg = appstate_config();
    app_runtime_t *rt  = appstate_runtime();
    for (int r = 0; r < RELAY_COUNT; r++) {
        bool desired;
        rt->relay_report[r] = appstate_resolve((relay_mode_t)cfg->relay_manual[r],
                                               (relay_mode_t)cfg->relay_macro[r],
                                               time_valid && auto_on[r], &desired);
        phys[r] = desired ? 1 : 0;
        rt->relay_physical[r] = phys[r];
    }
    appstate_unlock();

    relays_apply(phys);
}

static void controller_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "control loop started");
    for (;;) {
        evaluate_and_apply();
        // Wake early on notify, otherwise re-evaluate every second.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}

void controller_start(void)
{
    xTaskCreate(controller_task, "controller", 4096, NULL, 6, &s_task);
}

void controller_notify(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
