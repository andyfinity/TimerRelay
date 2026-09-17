// This codebase was primarily written by Claude Opus 4.8 (Anthropic).
const { InstanceBase, Regex, runEntrypoint, InstanceStatus } = require('@companion-module/base')
const { getActions } = require('./actions')
const { getFeedbacks } = require('./feedbacks')
const { getVariableDefinitions } = require('./variables')
const { getPresets } = require('./presets')

const RELAY_COUNT = 6

class TimerRelayInstance extends InstanceBase {
	async init(config) {
		this.config = config
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
		}

		this.updateStatus(InstanceStatus.Connecting)
		this.setActionDefinitions(getActions(this))
		this.setFeedbackDefinitions(getFeedbacks(this))
		this.setVariableDefinitions(getVariableDefinitions(RELAY_COUNT))
		this.setPresetDefinitions(getPresets(RELAY_COUNT))

		this.startPolling()
	}

	async destroy() {
		this.stopPolling()
	}

	async configUpdated(config) {
		this.config = config
		this.startPolling()
	}

	getConfigFields() {
		return [
			{
				type: 'static-text',
				id: 'info',
				width: 12,
				label: 'About',
				value: 'Connects to a TimerRelayV2 controller over its LAN REST API (no authentication).',
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

			this.updateStatus(InstanceStatus.Ok)
			this.publishVariables()
			this.checkFeedbacks('relay_state', 'relay_energized', 'time_valid', 'ntp_reachable')
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
		this.setVariableValues(vals)
	}
}

runEntrypoint(TimerRelayInstance, [])
