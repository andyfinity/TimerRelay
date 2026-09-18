// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
const { InstanceBase, Regex, runEntrypoint, InstanceStatus } = require('@companion-module/base')
const { getActions } = require('./actions')
const { getFeedbacks } = require('./feedbacks')
const { getVariableDefinitions } = require('./variables')
const { getPresets } = require('./presets')

const RELAY_COUNT = 6

// Format a duration in seconds as H:MM:SS (or MM:SS under an hour).
function fmtDur(s) {
	s = Math.max(0, Math.round(s))
	const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
	const p = (n) => String(n).padStart(2, '0')
	return h > 0 ? `${h}:${p(m)}:${p(sec)}` : `${p(m)}:${p(sec)}`
}
function fmtAge(s) {
	if (s < 0) return '-'
	if (s < 60) return `${s}s ago`
	if (s < 3600) return `${Math.floor(s / 60)}m ago`
	return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m ago`
}

class TimerRelayInstance extends InstanceBase {
	async init(config) {
		this.config = config
		this.macros = [] // [{id, label}] macro choices, refreshed from /api/config
		this.macroSig = ''
		this.pollCount = 0
		// Cached device state, refreshed by polling.
		this.state = {
			relays: [], // [{id, mode, report, physical}]
			time_valid: false,
			time_source: 'none',
			ntp_reachable: false,
			net_state: 'boot',
			ip: '',
			local: '',
			tz_name: '',
			macro_active: false,
			macro_index: -1,
			macro_name: '',
			macro_step: 0,
			macro_steps: 0,
			macro_run: 'auto',
			next_event_valid: false,
			next_event_in: 0, // seconds remaining reported by the device
			next_event_stamp: 0, // Date.now() when next_event_in was read
			next_event_at: '',
			next_event_desc: '',
			ntp_reliable: false,
			ntp_last_sync_age: -1,
			fw_version: '',
			fw_partition: '',
		}

		this.updateStatus(InstanceStatus.Connecting)
		await this.fetchConfig() // populate macro choices before defining actions
		this.updateDefinitions()
		this.startPolling()
		this.startCountdown()
	}

	async destroy() {
		this.stopPolling()
		this.stopCountdown()
	}

	async configUpdated(config) {
		this.config = config
		await this.fetchConfig()
		this.updateDefinitions()
		this.startPolling()
	}

	updateDefinitions() {
		this.setActionDefinitions(getActions(this))
		this.setFeedbackDefinitions(getFeedbacks(this))
		this.setVariableDefinitions(getVariableDefinitions(RELAY_COUNT))
		this.setPresetDefinitions(getPresets(RELAY_COUNT, this.macros))
	}

	// Refresh the macro list; rebuild definitions only when it actually changes.
	async fetchConfig() {
		const base = this.baseUrl()
		if (!base) return
		try {
			const res = await fetch(base + '/api/config', { signal: AbortSignal.timeout(4000) })
			if (!res.ok) throw new Error(`HTTP ${res.status}`)
			const c = await res.json()
			const list = (c.macros || []).map((m) => ({ id: m.index, label: m.name || `Macro ${m.index}` }))
			const sig = JSON.stringify(list)
			if (sig !== this.macroSig) {
				this.macros = list
				this.macroSig = sig
				this.updateDefinitions()
			}
		} catch (e) {
			// Non-fatal; /api/status polling reports connection health.
		}
	}

	async macroControl(action, macroId) {
		const body = action === 'stop' ? { action } : { action, macro: Number(macroId) }
		return this.apiPost('/api/macro', body)
	}

	getConfigFields() {
		return [
			{
				type: 'static-text',
				id: 'info',
				width: 12,
				label: 'About',
				value: 'Connects to a TimerRelay controller over its LAN REST API (no authentication).',
			},
			{
				type: 'textinput',
				id: 'host',
				label: 'Device IP address or hostname',
				width: 8,
				default: '',
				regex: Regex.HOSTNAME,
			},
			{
				type: 'number',
				id: 'poll',
				label: 'Poll interval (ms)',
				width: 4,
				default: 1000,
				min: 250,
				max: 60000,
			},
		]
	}

	baseUrl() {
		const host = (this.config.host || '').trim()
		return host ? `http://${host}` : null
	}

	// --- REST helpers --------------------------------------------------------
	async apiPost(path, body) {
		const base = this.baseUrl()
		if (!base) {
			this.updateStatus(InstanceStatus.BadConfig, 'No host set')
			return false
		}
		try {
			const res = await fetch(base + path, {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify(body),
				signal: AbortSignal.timeout(4000),
			})
			if (!res.ok) throw new Error(`HTTP ${res.status}`)
			// Refresh promptly so feedback reflects the change.
			this.pollNow()
			return true
		} catch (e) {
			this.log('error', `POST ${path} failed: ${e.message}`)
			this.updateStatus(InstanceStatus.ConnectionFailure, e.message)
			return false
		}
	}

	async setRelayMode(relay, mode) {
		return this.apiPost('/api/relay', { relay: Number(relay), mode })
	}

	// --- Polling -------------------------------------------------------------
	startPolling() {
		this.stopPolling()
		const interval = Math.max(250, Number(this.config.poll) || 1000)
		this.pollNow()
		this.pollTimer = setInterval(() => this.pollNow(), interval)
	}

	stopPolling() {
		if (this.pollTimer) {
			clearInterval(this.pollTimer)
			this.pollTimer = null
		}
	}

	async pollNow() {
		const base = this.baseUrl()
		if (!base) {
			this.updateStatus(InstanceStatus.BadConfig, 'No host set')
			return
		}
		try {
			const res = await fetch(base + '/api/status', { signal: AbortSignal.timeout(4000) })
			if (!res.ok) throw new Error(`HTTP ${res.status}`)
			const s = await res.json()
			this.state.relays = s.relays || []
			this.state.time_valid = !!s.time_valid
			this.state.time_source = s.time_source || 'none'
			this.state.ntp_reachable = !!s.ntp_reachable
			this.state.net_state = s.net_state || 'boot'
			this.state.ip = s.ip || ''
			this.state.local = s.local || ''
			this.state.tz_name = s.tz_name || ''

			const m = s.macro || {}
			this.state.macro_active = !!m.active
			this.state.macro_index = m.index != null ? m.index : -1
			this.state.macro_name = m.name || ''
			this.state.macro_step = m.step || 0
			this.state.macro_steps = m.steps || 0
			this.state.macro_run = m.run || 'auto'

			const ne = s.next_event || {}
			this.state.next_event_valid = !!ne.valid
			this.state.next_event_in = ne.in != null ? ne.in : 0
			this.state.next_event_stamp = Date.now()
			this.state.next_event_at = ne.local || ''
			this.state.next_event_desc = ne.desc || ''

			this.state.ntp_reliable = !!s.ntp_reliable
			this.state.ntp_last_sync_age = s.ntp_last_sync_age != null ? s.ntp_last_sync_age : -1
			this.state.fw_version = s.fw_version || ''
			this.state.fw_partition = s.fw_partition || ''

			this.updateStatus(InstanceStatus.Ok)
			this.publishVariables()
			this.publishCountdown()
			this.checkFeedbacks('relay_state', 'relay_energized', 'relay_manual', 'time_valid',
				'ntp_reachable', 'ntp_reliable', 'macro_active', 'macro_running')

			// Periodically resync macro choices in case they were edited elsewhere.
			if (this.pollCount++ % 15 === 0) this.fetchConfig()
		} catch (e) {
			this.updateStatus(InstanceStatus.ConnectionFailure, e.message)
		}
	}

	relayById(id) {
		return this.state.relays.find((r) => Number(r.id) === Number(id))
	}

	publishVariables() {
		const vals = {}
		for (let i = 1; i <= RELAY_COUNT; i++) {
			const r = this.relayById(i)
			vals[`relay${i}_mode`] = r ? r.mode : '?'
			vals[`relay${i}_report`] = r ? r.report : '?'
			vals[`relay${i}_energized`] = r ? (r.physical ? 'on' : 'off') : '?'
		}
		vals['time_valid'] = this.state.time_valid ? 'yes' : 'no'
		vals['time_source'] = this.state.time_source
		vals['ntp_reachable'] = this.state.ntp_reachable ? 'yes' : 'no'
		vals['net_state'] = this.state.net_state
		vals['ip'] = this.state.ip
		vals['local_time'] = this.state.local
		vals['timezone'] = this.state.tz_name
		vals['macro_active'] = this.state.macro_active ? 'yes' : 'no'
		vals['macro_name'] = this.state.macro_active ? this.state.macro_name : '-'
		vals['macro_index'] = this.state.macro_active ? this.state.macro_index : -1
		vals['macro_step'] = this.state.macro_steps ? `${this.state.macro_step}/${this.state.macro_steps}` : '-'
		vals['macro_step_num'] = this.state.macro_active ? this.state.macro_step : 0
		vals['macro_steps_total'] = this.state.macro_active ? this.state.macro_steps : 0
		vals['macro_run'] = this.state.macro_active ? this.state.macro_run : '-'
		vals['next_event_at'] = this.state.next_event_valid ? this.state.next_event_at : '-'
		vals['next_event_desc'] = this.state.next_event_valid ? this.state.next_event_desc : '-'
		vals['ntp_reliable'] = this.state.ntp_reliable ? 'yes' : 'no'
		vals['ntp_last_sync'] = fmtAge(this.state.ntp_last_sync_age)
		vals['firmware_version'] = this.state.fw_version || '?'
		vals['firmware_slot'] = this.state.fw_partition || '?'
		this.setVariableValues(vals)
	}

	// Local 1 Hz countdown so "time to next event" ticks smoothly between polls.
	startCountdown() {
		this.stopCountdown()
		this.countdownTimer = setInterval(() => this.publishCountdown(), 1000)
	}
	stopCountdown() {
		if (this.countdownTimer) {
			clearInterval(this.countdownTimer)
			this.countdownTimer = null
		}
	}
	publishCountdown() {
		if (!this.state.next_event_valid) {
			this.setVariableValues({ next_event_in: '-', next_event_secs: -1 })
			return
		}
		const elapsed = (Date.now() - this.state.next_event_stamp) / 1000
		const remain = Math.max(0, this.state.next_event_in - elapsed)
		this.setVariableValues({ next_event_in: fmtDur(remain), next_event_secs: Math.round(remain) })
	}
}

runEntrypoint(TimerRelayInstance, [])
