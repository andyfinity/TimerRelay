// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
const { combineRgb } = require('@companion-module/base')
const { RELAY_CHOICES } = require('./actions')

const REPORT_CHOICES = [
	{ id: 'auto_on', label: 'Auto · On' },
	{ id: 'auto_off', label: 'Auto · Off' },
	{ id: 'macro_on', label: 'Macro · On' },
	{ id: 'macro_off', label: 'Macro · Off' },
	{ id: 'manual_on', label: 'Manual · On' },
	{ id: 'manual_off', label: 'Manual · Off' },
]

function getFeedbacks(self) {
	const macros = self.macros && self.macros.length ? self.macros : [{ id: 0, label: '(no macros defined)' }]
	return {
		// The core feedback requested: match one of the reported states.
		relay_state: {
			type: 'boolean',
			name: 'Relay is in state',
			description: 'True when the selected relay reports the selected state (auto/macro/manual x on/off).',
			defaultStyle: {
				bgcolor: combineRgb(0, 120, 60),
				color: combineRgb(255, 255, 255),
			},
			options: [
				{ type: 'dropdown', id: 'relay', label: 'Relay', default: 1, choices: RELAY_CHOICES },
				{ type: 'dropdown', id: 'state', label: 'Reported state', default: 'auto_on', choices: REPORT_CHOICES },
			],
			callback: (fb) => {
				const r = self.relayById(fb.options.relay)
				return !!r && r.report === fb.options.state
			},
		},

		// Simple "is the coil energized" feedback, regardless of auto/manual.
		relay_energized: {
			type: 'boolean',
			name: 'Relay is energized (enabled / NO)',
			description: 'True when the selected relay is currently energized.',
			defaultStyle: {
				bgcolor: combineRgb(0, 150, 80),
				color: combineRgb(255, 255, 255),
			},
			options: [{ type: 'dropdown', id: 'relay', label: 'Relay', default: 1, choices: RELAY_CHOICES }],
			callback: (fb) => {
				const r = self.relayById(fb.options.relay)
				return !!r && !!r.physical
			},
		},

		// Whether the relay is under manual override (either direction).
		relay_manual: {
			type: 'boolean',
			name: 'Relay is under manual override',
			description: 'True when the selected relay is forced On or Off (not Auto).',
			defaultStyle: {
				bgcolor: combineRgb(180, 130, 0),
				color: combineRgb(255, 255, 255),
			},
			options: [{ type: 'dropdown', id: 'relay', label: 'Relay', default: 1, choices: RELAY_CHOICES }],
			callback: (fb) => {
				const r = self.relayById(fb.options.relay)
				return !!r && (r.mode === 'on' || r.mode === 'off')
			},
		},

		time_valid: {
			type: 'boolean',
			name: 'Clock is valid',
			description: 'True when the device has a valid time (NTP or manual).',
			defaultStyle: { bgcolor: combineRgb(0, 100, 160), color: combineRgb(255, 255, 255) },
			options: [],
			callback: () => self.state.time_valid,
		},

		ntp_reachable: {
			type: 'boolean',
			name: 'NTP reachable',
			description: 'True when NTP has synced at least once.',
			defaultStyle: { bgcolor: combineRgb(0, 100, 160), color: combineRgb(255, 255, 255) },
			options: [],
			callback: () => self.state.ntp_reachable,
		},
		ntp_reliable: {
			type: 'boolean',
			name: 'NTP reliably connected',
			description: 'True when the last NTP sync is recent enough to trust. Use the inverted style (or a second feedback) to flag a stale/lost time source.',
			defaultStyle: { bgcolor: combineRgb(0, 130, 90), color: combineRgb(255, 255, 255) },
			options: [],
			callback: () => self.state.ntp_reliable,
		},

		macro_active: {
			type: 'boolean',
			name: 'A macro is running',
			description: 'True while any macro is active.',
			defaultStyle: { bgcolor: combineRgb(120, 80, 200), color: combineRgb(255, 255, 255) },
			options: [],
			callback: () => self.state.macro_active,
		},
		macro_running: {
			type: 'boolean',
			name: 'Specific macro is running',
			description: 'True when the selected macro is the active one.',
			defaultStyle: { bgcolor: combineRgb(120, 80, 200), color: combineRgb(255, 255, 255) },
			options: [{ type: 'dropdown', id: 'macro', label: 'Macro', default: macros[0].id, choices: macros }],
			callback: (fb) =>
				self.state.macro_active && Number(self.state.macro_index) === Number(fb.options.macro),
		},
	}
}

module.exports = { getFeedbacks, REPORT_CHOICES }
