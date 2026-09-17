// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
const RELAY_CHOICES = Array.from({ length: 6 }, (_, i) => ({ id: i + 1, label: `Relay ${i + 1}` }))
const MODE_CHOICES = [
	{ id: 'on', label: 'On (manual override, energized / NO)' },
	{ id: 'auto', label: 'Auto (follow schedule)' },
	{ id: 'off', label: 'Off (manual override, de-energized / NC)' },
]

function getActions(self) {
	return {
		set_relay_mode: {
			name: 'Set relay mode',
			options: [
				{ type: 'dropdown', id: 'relay', label: 'Relay', default: 1, choices: RELAY_CHOICES },
				{ type: 'dropdown', id: 'mode', label: 'Mode', default: 'auto', choices: MODE_CHOICES },
			],
			callback: async (action) => {
				await self.setRelayMode(action.options.relay, action.options.mode)
			},
		},

		// Convenience: cycle a relay On -> Off -> Auto -> On.
		cycle_relay_mode: {
			name: 'Cycle relay mode (On/Off/Auto)',
			options: [{ type: 'dropdown', id: 'relay', label: 'Relay', default: 1, choices: RELAY_CHOICES }],
			callback: async (action) => {
				const r = self.relayById(action.options.relay)
				const next = { on: 'off', off: 'auto', auto: 'on' }
				const mode = next[r ? r.mode : 'auto'] || 'auto'
				await self.setRelayMode(action.options.relay, mode)
			},
		},

		// Set every relay to the same mode at once.
		set_all_relays: {
			name: 'Set all relays',
			options: [{ type: 'dropdown', id: 'mode', label: 'Mode', default: 'auto', choices: MODE_CHOICES }],
			callback: async (action) => {
				for (let i = 1; i <= 6; i++) {
					await self.setRelayMode(i, action.options.mode)
				}
			},
		},
	}
}

module.exports = { getActions, RELAY_CHOICES, MODE_CHOICES }
