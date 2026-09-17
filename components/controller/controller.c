// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
#include "controller.h"
#include "appstate.h"
#include "relays.h"
#include "schedule.h"
#include "timekeeper.h"

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "controller";
static TaskHandle_t s_task;

static void evaluate_and_apply(void)
{
    time_t now = time(NULL);
    bool time_valid = timekeeper_time_valid();

    bool auto_on[RELAY_COUNT] = {0};
    if (time_valid) {
        schedule_eval(now, auto_on);
    }
    // When time is invalid, auto_on stays all-false: AUTO relays hold the safe
    // disabled state until the clock is known. Manual overrides still apply.

    uint8_t phys[RELAY_COUNT];
    appstate_lock();
    app_config_t  *cfg = appstate_config();
    app_runtime_t *rt  = appstate_runtime();
    for (int r = 0; r < RELAY_COUNT; r++) {
        relay_mode_t mode = (relay_mode_t)cfg->relay_mode[r];
        bool desired;
        switch (mode) {
            case RELAY_MODE_ON:  desired = true;  break;
            case RELAY_MODE_OFF: desired = false; break;
            case RELAY_MODE_AUTO:
            default:             desired = time_valid && auto_on[r]; break;
        }
        phys[r] = desired ? 1 : 0;
        rt->relay_physical[r] = phys[r];
        rt->relay_report[r] = appstate_report_for(mode, time_valid && auto_on[r]);
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
