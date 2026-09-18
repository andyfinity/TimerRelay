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
	defs.push({ variableId: 'net_state', name: 'Network state' })
	defs.push({ variableId: 'ip', name: 'IP address' })
	defs.push({ variableId: 'local_time', name: 'Device local time' })
	defs.push({ variableId: 'timezone', name: 'Timezone name' })
	defs.push({ variableId: 'macro_active', name: 'Macro running (yes/no)' })
	defs.push({ variableId: 'macro_name', name: 'Active macro name' })
	defs.push({ variableId: 'macro_step', name: 'Active macro step (x/y)' })
	defs.push({ variableId: 'macro_run', name: 'Active macro run mode' })
	return defs
}

module.exports = { getVariableDefinitions }
