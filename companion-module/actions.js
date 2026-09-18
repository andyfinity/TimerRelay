// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
const RELAY_CHOICES = Array.from({ length: 6 }, (_, i) => ({ id: i + 1, label: `Relay ${i + 1}` }))
const MODE_CHOICES = [
	{ id: 'on', label: 'On (manual override, energized / NO)' },
	{ id: 'auto', label: 'Auto (follow schedule)' },
	{ id: 'off', label: 'Off (manual override, de-energized / NC)' },
]

// Macro choices come from the device (refreshed by main.js); fall back to a
// placeholder so the dropdown is never empty.
function macroChoices(self) {
	return self.macros && self.macros.length ? self.macros : [{ id: 0, label: '(no macros defined)' }]
}

function getActions(self) {
	const macros = macroChoices(self)
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

		// --- Macros ---
		macro_start: {
			name: 'Macro: start',
			options: [{ type: 'dropdown', id: 'macro', label: 'Macro', default: macros[0].id, choices: macros }],
			callback: async (action) => {
				await self.macroControl('start', action.options.macro)
			},
		},
		macro_step: {
			name: 'Macro: step (advance one step)',
			description: 'Advance the running macro one step; if none is running, start the selected macro paused at its first step.',
			options: [{ type: 'dropdown', id: 'macro', label: 'Macro (used only if none running)', default: macros[0].id, choices: macros }],
			callback: async (action) => {
				await self.macroControl('step', action.options.macro)
			},
		},
		macro_stop: {
			name: 'Macro: stop',
			options: [],
			callback: async () => {
				await self.macroControl('stop')
			},
		},
	}
}

module.exports = { getActions, RELAY_CHOICES, MODE_CHOICES }
