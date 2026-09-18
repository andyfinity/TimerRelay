# TimerRelay

Control and monitor a **TimerRelay** 6-channel Wi-Fi relay controller (Seeed XIAO ESP32C6) from Bitfocus Companion.

## Configuration

| Field | Description |
|-------|-------------|
| **Device IP address or hostname** | The controller's address on your LAN (e.g. `192.168.1.50` or `timerrelay.local`). Shown on the device's own web page. |
| **Poll interval (ms)** | How often to refresh state for variables and feedback. Default 1000 ms. |

The controller's REST API is unauthenticated (intended for a trusted LAN), so no
credentials are required.

## Actions

- **Set relay mode** — set a relay to *On* (manual, energized/NO), *Auto* (follow the schedule), or *Off* (manual, de-energized/NC).
- **Cycle relay mode** — cycle a relay On → Off → Auto → On.
- **Set all relays** — apply one mode to all six relays.
- **Macro: start** — start the selected macro (auto-runs through its timed steps).
- **Macro: step** — advance the running macro one step; if none is running, start the selected macro paused at step 1.
- **Macro: stop** — stop the active macro (relays keep their last macro-set state).

Macro choices are read live from the device and refresh automatically.

## Feedbacks

- **Relay is in state** — true when a relay reports **Auto·On**, **Auto·Off**, **Manual·On**, or **Manual·Off** (the four states requested for feedback).
- **Relay is energized** — true when the coil is currently energized.
- **Relay is under manual override** — true when a relay is forced On or Off.
- **Clock is valid** / **NTP reachable** — device time status.
- **NTP reliably connected** — true only while the last NTP sync is recent enough to trust; invert the style to flag a stalled or lost time source.
- **A macro is running** — true while any macro is active.
- **Specific macro is running** — true when the selected macro is the active one.

## Variables

Per relay: `relay1_mode`, `relay1_report`, `relay1_energized`, … through relay 6.
Time / NTP: `time_valid`, `time_source`, `ntp_reachable`, `ntp_reliable`, `ntp_last_sync` (e.g. `4m ago`), `local_time`, `timezone`.
Next scheduled event: `next_event_in` (H:MM:SS countdown, ticks every second), `next_event_secs`, `next_event_at` (local time), `next_event_desc` (e.g. `R1,R2 on` or `Macro: Blink`).
Network: `net_state`, `ip`.
Macros: `macro_active`, `macro_name`, `macro_index`, `macro_step` (e.g. `2/3`), `macro_step_num`, `macro_steps_total`, `macro_run` (auto/manual).

## Presets

- An On / Auto / Off button set per relay, pre-wired with state colouring.
- Per macro: **Start** and **Step** buttons (Start colours itself while that macro runs), plus a global **Stop macro** button.
