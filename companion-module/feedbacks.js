const { combineRgb } = require('@companion-module/base')
const { RELAY_CHOICES } = require('./actions')

const REPORT_CHOICES = [
	{ id: 'auto_on', label: 'Auto · On' },
	{ id: 'auto_off', label: 'Auto · Off' },
	{ id: 'manual_on', label: 'Manual · On' },
	{ id: 'manual_off', label: 'Manual · Off' },
]

function getFeedbacks(self) {
	return {
		// The core feedback requested: match one of the four reported states.
		relay_state: {
			type: 'boolean',
			name: 'Relay is in state',
			description: 'True when the selected relay reports the selected state (auto/manual x on/off).',
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
			description: 'True when NTP has synced successfully.',
			defaultStyle: { bgcolor: combineRgb(0, 100, 160), color: combineRgb(255, 255, 255) },
			options: [],
			callback: () => self.state.ntp_reachable,
		},
	}
}

module.exports = { getFeedbacks, REPORT_CHOICES }
