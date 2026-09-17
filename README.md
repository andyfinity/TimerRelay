> _This codebase was primarily written by Claude Opus 4.8 (Anthropic)._

# TimerRelayV2

Timer-driven controller for the **Seeed Studio XIAO ESP32C6 + 6-channel Wi-Fi 5V relay** board.
Switches relays on a weekly schedule (in local time, DST-aware), with manual
overrides, a self-hosted dark-theme config site, a REST API, and a Bitfocus
Companion module. Built with **ESP-IDF v6.0.2**.

## Features

- **6 relays**, each enabled (NO) / disabled (NC) on a weekly schedule. The event
  model already carries daily / monthly / yearly / one-time recurrence for future
  expansion; the evaluator implements all of them.
- **Per-relay override**: On / Auto / Off, from the web UI, REST API, or Companion.
- **Timekeeping** via NTP over Wi-Fi, with a manual-set fallback and a curated,
  DST-correct timezone list (POSIX TZ strings).
- **Networking**: joins your Wi-Fi as a station; if none is configured or the join
  times out, it hosts its own open **`TimerRelay-XXXX`** network with a captive
  portal for setup.
- **Persistence**: settings and relay override modes survive power cycles, stored
  in NVS with write-on-change and a deliberately oversized partition so wear
  leveling spreads erase cycles — suited to a long service life.
- **Simultaneity**: one 1 Hz control loop recomputes the whole 6-relay vector and
  applies it at once, so relays scheduled for the same second switch together
  (accuracy ±1 s).

## Hardware map (from `Docs/`)

| Relay | XIAO pin | ESP32C6 GPIO | Driver |
|------:|:--------:|:------------:|:-------|
| 1 (K1) | D2  | GPIO2  | S9013 NPN, active-HIGH |
| 2 (K2) | D3  | GPIO21 | " |
| 3 (K3) | D1  | GPIO1  | " |
| 4 (K4) | D0  | GPIO0  | " |
| 5 (K5) | D8  | GPIO19 | " |
| 6 (K6) | D10 | GPIO18 | " |

GPIO **high → coil energized → COM–NO (enabled)**; low → COM–NC (disabled), which
is the safe power-on default. Console is on the USB Serial/JTAG port (no UART pins
used); D6/D7 are the onboard buttons; D4/D5 are the (unused) I²C pins.

## Build & flash

```bash
. /home/russan/.espressif/v6.0.2/esp-idf/export.sh
idf.py set-target esp32c6      # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

In VS Code, the ESP-IDF extension tasks are pre-configured in `.vscode/`
(Build / Flash+Monitor / Menuconfig / Fullclean).

## REST API (unauthenticated, trusted-LAN)

Plain HTTP on **port 80**, JSON in and out, no authentication (by design — this is
a trusted-LAN device). The base URL is the device's address, `http://<ip>` or
`http://timerrelay.local` (the hostname from `/api/status`). This is the same API
the built-in web UI and the Companion module use.

Quick reference:

| Method | Path | Body | Purpose |
|--------|------|------|---------|
| GET  | `/api/status`   | — | Full runtime state (relays, time, network) |
| GET  | `/api/config`   | — | Schedule + settings |
| GET  | `/api/tzlist`   | — | Available timezones |
| POST | `/api/relay`    | `{"relay":1-6,"mode":"on\|auto\|off"}` | Set override |
| POST | `/api/schedule` | `{"events":[…]}` | Replace schedule |
| POST | `/api/time`     | `{"epoch":<utc>}` | Manual time set |
| POST | `/api/timezone` | `{"name":"America/New_York"}` | Set timezone |
| POST | `/api/wifi`     | `{"ssid":"…","pass":"…"}` | Set Wi-Fi + reconnect |

All POST bodies use `Content-Type: application/json` and return `{"ok":true}` on
success or HTTP **400** with a short reason on bad input. Every successful write is
persisted to NVS (write-on-change) and immediately re-evaluated by the 1 Hz control
loop, so changes take effect within a fraction of a second. JSON responses carry
`Cache-Control: no-store`.

### GET `/api/status`

The full live state — poll this (the UI polls every 2 s).

| Field | Type | Meaning |
|---|---|---|
| `time_valid` | bool | Clock is believed correct (NTP synced or manually set) |
| `time_source` | `"ntp"` \| `"manual"` \| `"none"` | Where the current time came from |
| `ntp_reachable` | bool | NTP has synced at least once |
| `epoch` | number | Current UTC epoch seconds |
| `local` | string | `"YYYY-MM-DD HH:MM:SS"` in the device's timezone |
| `tz_name` | string | e.g. `"America/New_York"` |
| `net_state` | `"sta_connected"` \| `"ap_fallback"` \| `"connecting"` \| `"boot"` | Wi-Fi state |
| `ip` | string | Current IP (STA IP, or `192.168.4.1` in AP fallback) |
| `ap_ssid` | string | The `TimerRelay-XXXX` SSID when hosting its own network |
| `hostname` | string | Device hostname |
| `wifi_ssid` | string | Configured station SSID |
| `relays` | array | Six objects (below) |

Each `relays[]` entry: `id` (1–6), `mode` (`"auto"`/`"on"`/`"off"` — the override
setting), `report` (`"auto_on"`/`"auto_off"`/`"manual_on"`/`"manual_off"` — the
state used for Companion feedback), and `physical` (bool — coil currently
energized).

```bash
curl http://timerrelay.local/api/status
```

### GET `/api/config`

The schedule-editor payload: `hostname`, `tz_name`, `wifi_ssid`, and the full
`events` array. Each event: `enabled` (bool), `type` (`"weekly"` today;
`daily`/`monthly`/`yearly`/`oneshot` reserved), `action` (`"on"` = enable/NO,
`"off"` = disable/NC), `relays` (array of 1-based ids), `dow` (array,
**0=Sunday … 6=Saturday**), `hour`/`minute`/`second`, and `day`/`month`/`year`
(used by the non-weekly types).

```bash
curl http://timerrelay.local/api/config
```

### GET `/api/tzlist`

The curated timezone names for the picker: `{ "zones": ["UTC", "America/New_York", …] }`.
Use one of these exact strings when setting the timezone.

```bash
curl http://timerrelay.local/api/tzlist
```

### POST `/api/relay` — set one relay's override

Body: `{"relay": 1-6, "mode": "on"|"auto"|"off"}`. `on`/`off` are manual overrides
that always apply; `auto` returns the relay to schedule control.

```bash
curl -X POST http://timerrelay.local/api/relay -H 'Content-Type: application/json' -d '{"relay":3,"mode":"on"}'
```

### POST `/api/schedule` — replace the entire schedule

Body: `{"events":[ … ]}` using the event shape from `/api/config`. This **replaces
all events** (not an append); max 64. Out-of-range numeric fields are clamped. A
relay follows the most-recently-fired applicable event, so to turn something off
again you add a second `"action":"off"` event at the later time.

```bash
curl -X POST http://timerrelay.local/api/schedule -H 'Content-Type: application/json' -d '{"events":[{"enabled":true,"type":"weekly","action":"on","relays":[1,2],"dow":[1,2,3,4,5],"hour":8,"minute":0,"second":0}]}'
```

### POST `/api/time` — set the clock manually

Body: `{"epoch": <UTC seconds>}`. Marks the time valid with `time_source:"manual"`.
(No battery RTC, so a manual clock is lost on power-off until NTP or another manual
set.)

```bash
curl -X POST http://timerrelay.local/api/time -H 'Content-Type: application/json' -d "{\"epoch\":$(date -u +%s)}"
```

### POST `/api/timezone` — set the timezone

Body: `{"name": "<one of /api/tzlist>"}`. Applies the POSIX/DST rules live. Unknown
names return 400.

```bash
curl -X POST http://timerrelay.local/api/timezone -H 'Content-Type: application/json' -d '{"name":"America/New_York"}'
```

### POST `/api/wifi` — set station credentials

Body: `{"ssid":"…","pass":"…"}` (empty `pass` = open network). It **acks first, then
flips the Wi-Fi** to join the new network — so afterward the device may move to a
different IP and the connection you made the request on will drop.

```bash
curl -X POST http://timerrelay.local/api/wifi -H 'Content-Type: application/json' -d '{"ssid":"Studio","pass":"hunter2"}'
```

### Static & captive-portal routes

`GET /`, `/index.html`, `/style.css`, `/app.js` serve the UI. Common OS
captive-portal probes (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, …) and
any other GET redirect to `http://192.168.4.1/` while in AP-setup mode.

> Terminology across the whole API: "enable"/`on`/NO = coil energized (COM→NO);
> "disable"/`off`/NC = de-energized (COM→NC), which is also the safe power-on and
> no-valid-time default.

## Companion module

See [`companion-module/`](companion-module/) — actions to set relay modes and
boolean feedbacks for **auto-on / auto-off / manual-on / manual-off**, plus
per-relay presets. It talks to the same REST API.

## Project layout

```
main/                     app_main: boot order + wiring
components/
  appstate/   central mutex-protected config + runtime state, NVS persistence
  storage/    thin NVS wrapper
  relays/     GPIO driver (hardware map, active-high, safe default)
  schedule/   recurrence evaluator -> desired auto-state vector
  timekeeper/ SNTP + manual time + POSIX TZ (curated list in tzdata.c)
  netmgr/     Wi-Fi STA, AP fallback, captive-portal DNS
  controller/ 1 Hz loop: schedule + overrides -> relays
  webserver/  embedded dark-theme site (www/) + REST API
  cjson/      vendored cJSON (MIT)
companion-module/         Bitfocus Companion v3 module
partitions.csv            oversized NVS for wear leveling
```

## Design notes

- **No battery RTC on this board.** A manually set clock is lost on power-off
  until NTP resyncs or it is set again. While the clock is invalid, AUTO relays
  hold the safe disabled state; manual On/Off overrides still apply.
- Relay override **modes** persist separately from settings so toggling an
  override rewrites only a few bytes, not the whole schedule blob.
