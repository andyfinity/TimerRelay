// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
// TimerRelayV2 - Seeed XIAO ESP32C6 + 6-channel relay timer/controller.
//
// Boot order is chosen so the relays reach their safe (disabled) state as early
// as possible, then persisted config loads, then the control loop, network, and
// web/REST server come up.
#include "esp_log.h"

#include "storage.h"
#include "appstate.h"
#include "relays.h"
#include "timekeeper.h"
#include "controller.h"
#include "netmgr.h"
#include "webserver.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "TimerRelayV2 starting");

    relays_init();          // drive all outputs low (disabled) immediately
    storage_init();         // NVS
    appstate_init();        // load persisted config + relay modes
    timekeeper_init();      // apply timezone, evaluate clock validity

    controller_start();     // 1 Hz loop: schedule + overrides -> relays
    netmgr_init();          // Wi-Fi STA, AP fallback, captive portal
    webserver_start();      // dark-theme config site + REST API

    ESP_LOGI(TAG, "startup complete");
}
