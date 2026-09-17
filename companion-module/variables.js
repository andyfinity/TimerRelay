// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
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
	return defs
}

module.exports = { getVariableDefinitions }
