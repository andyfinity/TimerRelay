// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
const { combineRgb } = require('@companion-module/base')

// One preset row per relay: On / Auto / Off buttons that also colour themselves
// from the live reported state.
function getPresets(relayCount, macros = []) {
	const presets = {}
	const white = combineRgb(255, 255, 255)
	const black = combineRgb(0, 0, 0)

	for (let i = 1; i <= relayCount; i++) {
		const cat = `Relay ${i}`

		presets[`relay${i}_on`] = {
			type: 'button',
			category: cat,
			name: `Relay ${i} On`,
			style: { text: `R${i}\\nON`, size: '18', color: white, bgcolor: black },
			steps: [{ down: [{ actionId: 'set_relay_mode', options: { relay: i, mode: 'on' } }], up: [] }],
			feedbacks: [
				{
					feedbackId: 'relay_state',
					options: { relay: i, state: 'manual_on' },
					style: { bgcolor: combineRgb(0, 150, 80), color: white },
				},
			],
		}

		presets[`relay${i}_auto`] = {
			type: 'button',
			category: cat,
			name: `Relay ${i} Auto`,
			style: { text: `R${i}\\nAUTO`, size: '18', color: white, bgcolor: black },
			steps: [{ down: [{ actionId: 'set_relay_mode', options: { relay: i, mode: 'auto' } }], up: [] }],
			feedbacks: [
				{
					feedbackId: 'relay_state',
					options: { relay: i, state: 'auto_on' },
					style: { bgcolor: combineRgb(0, 90, 180), color: white },
				},
				{
					feedbackId: 'relay_state',
					options: { relay: i, state: 'auto_off' },
					style: { bgcolor: combineRgb(40, 40, 60), color: white },
				},
			],
		}

		presets[`relay${i}_off`] = {
			type: 'button',
			category: cat,
			name: `Relay ${i} Off`,
			style: { text: `R${i}\\nOFF`, size: '18', color: white, bgcolor: black },
			steps: [{ down: [{ actionId: 'set_relay_mode', options: { relay: i, mode: 'off' } }], up: [] }],
			feedbacks: [
				{
					feedbackId: 'relay_state',
					options: { relay: i, state: 'manual_off' },
					style: { bgcolor: combineRgb(150, 110, 0), color: white },
				},
			],
		}
	}

	// --- Macro presets ---
	const purple = combineRgb(120, 80, 200)
	for (const m of macros) {
		presets[`macro_start_${m.id}`] = {
			type: 'button',
			category: 'Macros',
			name: `Start: ${m.label}`,
			style: { text: `▶\\n${m.label}`, size: '14', color: white, bgcolor: black },
			steps: [{ down: [{ actionId: 'macro_start', options: { macro: m.id } }], up: [] }],
			feedbacks: [
				{ feedbackId: 'macro_running', options: { macro: m.id }, style: { bgcolor: purple, color: white } },
			],
		}
		presets[`macro_step_${m.id}`] = {
			type: 'button',
			category: 'Macros',
			name: `Step: ${m.label}`,
			style: { text: `⏭\\n${m.label}`, size: '14', color: white, bgcolor: black },
			steps: [{ down: [{ actionId: 'macro_step', options: { macro: m.id } }], up: [] }],
			feedbacks: [],
		}
	}
	presets['macro_stop'] = {
		type: 'button',
		category: 'Macros',
		name: 'Stop macro',
		style: { text: '■\\nSTOP', size: '18', color: white, bgcolor: black },
		steps: [{ down: [{ actionId: 'macro_stop', options: {} }], up: [] }],
		feedbacks: [
			{ feedbackId: 'macro_active', options: {}, style: { bgcolor: combineRgb(150, 40, 40), color: white } },
		],
	}

	return presets
}

module.exports = { getPresets }
