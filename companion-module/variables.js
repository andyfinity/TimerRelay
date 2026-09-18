// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
function getVariableDefinitions(relayCount) {
	const defs = []
	for (let i = 1; i <= relayCount; i++) {
		defs.push({ variableId: `relay${i}_mode`, name: `Relay ${i} mode (on/auto/off)` })
		defs.push({ variableId: `relay${i}_report`, name: `Relay ${i} reported state` })
		defs.push({ variableId: `relay${i}_energized`, name: `Relay ${i} energized (on/off)` })
	}
	defs.push({ variableId: 'time_valid', name: 'Clock valid (yes/no)' })
	defs.push({ variableId: 'time_source', name: 'Time source (ntp/manual/none)' })
	defs.push({ variableId: 'ntp_reachable', name: 'NTP reachable (yes/no)' })
	defs.push({ variableId: 'ntp_reliable', name: 'NTP reliably connected (yes/no)' })
	defs.push({ variableId: 'ntp_last_sync', name: 'Time since last NTP sync' })
	defs.push({ variableId: 'net_state', name: 'Network state' })
	defs.push({ variableId: 'ip', name: 'IP address' })
	defs.push({ variableId: 'firmware_version', name: 'Firmware version' })
	defs.push({ variableId: 'firmware_slot', name: 'Running OTA slot' })
	defs.push({ variableId: 'local_time', name: 'Device local time' })
	defs.push({ variableId: 'timezone', name: 'Timezone name' })
	defs.push({ variableId: 'next_event_in', name: 'Time to next scheduled event (H:MM:SS)' })
	defs.push({ variableId: 'next_event_secs', name: 'Seconds to next scheduled event' })
	defs.push({ variableId: 'next_event_at', name: 'Next scheduled event local time' })
	defs.push({ variableId: 'next_event_desc', name: 'Next scheduled event description' })
	defs.push({ variableId: 'macro_active', name: 'Macro running (yes/no)' })
	defs.push({ variableId: 'macro_name', name: 'Active macro name' })
	defs.push({ variableId: 'macro_index', name: 'Active macro index (-1 if none)' })
	defs.push({ variableId: 'macro_step', name: 'Active macro step (x/y)' })
	defs.push({ variableId: 'macro_step_num', name: 'Active macro current step number' })
	defs.push({ variableId: 'macro_steps_total', name: 'Active macro total steps' })
	defs.push({ variableId: 'macro_run', name: 'Active macro run mode' })
	return defs
}

module.exports = { getVariableDefinitions }
