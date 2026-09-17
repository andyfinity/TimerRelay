# CLAUDE.md — TimerRelayV2

Guidance for working in this repo.

## What this is
ESP-IDF **v6.0.2** firmware for a Seeed XIAO ESP32C6 driving a 6-channel relay
board, plus a Bitfocus Companion module. See `README.md` for the feature list and
hardware pin map.

## Building
Always source the IDF environment first:
```bash
. /home/russan/.espressif/v6.0.2/esp-idf/export.sh
idf.py build            # target is esp32c6 (already set)
```
Target is `esp32c6`. `sdkconfig` is generated from `sdkconfig.defaults`
(don't commit `sdkconfig`).

## Architecture (one-way dependency flow)
`main` → `webserver`/`netmgr`/`controller` → `schedule`/`timekeeper`/`relays` →
`appstate` → `storage`. `appstate` is the single source of truth: a mutex plus a
persisted `app_config_t` and a volatile `app_runtime_t`. Take `appstate_lock()`
around any access; keep critical sections short (the lock is recursive).

## Key invariants — keep these true
- **Relays are active-HIGH**; low = disabled (NC) = safe default. The GPIO map
  lives in `components/relays/relays.c` (`RELAY_GPIO[]`). Do not reorder it — it
  comes from the board schematic in `Docs/`.
- **One writer to the outputs**: only the `controller` task calls
  `relays_apply()`. It recomputes all six relays each tick and applies them
  together (same-second simultaneity). Don't drive GPIOs elsewhere.
- **Clock validity gates AUTO relays.** When `time_valid` is false, AUTO relays
  are forced off; manual overrides still apply. Preserve this in `controller.c`.
- **Write-on-change persistence.** Persist via `appstate_save_config()`, which
  diffs against shadow copies and splits settings vs. the frequently-changed
  relay-mode blob. Don't add unconditional NVS writes (flash wear).
- **Local-time scheduling.** `schedule.c` uses `mktime`/`localtime_r` with the TZ
  env set by `timekeeper`. All event times are local wall-clock.

## Schedule model
`sched_event_t` (in `appstate.h`) is a transition: at its time it sets its target
relays to `action`. A relay's auto state = the most recently fired applicable
event. WEEKLY is exposed in the UI; the evaluator already handles daily/monthly/
yearly/oneshot — surface them in the UI without touching the core when needed.

## Web UI
Embedded from `components/webserver/www/` via `EMBED_FILES` (symbols like
`_binary_index_html_start`). Vanilla HTML/CSS/JS, no build step. It polls
`/api/status` every 2 s. Edit the files directly and rebuild.

## REST API
Handlers in `components/webserver/webserver.c`. Unauthenticated by design
(trusted LAN). The Companion module in `companion-module/` uses the same API.

## Gotchas
- IDF v6 split `driver`: GPIO needs `esp_driver_gpio`, not `driver`.
- cJSON is not in IDF v6 core; it's vendored in `components/cjson/`.
- No Node.js in this environment — the Companion module can't be run/linted here;
  validate JS by review.
- NTP-from-DHCP needs `CONFIG_LWIP_DHCP_GET_NTP_SRV=y` (in `sdkconfig.defaults`)
  for `timekeeper.c`'s `server_from_dhcp = true` to work; `..._MAX_NTP_SERVERS`
  silently depends on it. Explicit `pool.ntp.org` works without it.
