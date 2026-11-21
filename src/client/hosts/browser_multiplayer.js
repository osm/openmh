'use strict';

// Browser API adapter; connection and race ownership remain in the application.
class OpenMHMultiplayer {
	static states = [
		'offline',
		'connecting',
		'lobby',
		'loading',
		'racing',
		'leaving',
		'failed'
	];

	constructor(player) {
		this.player = player;
		this.engaged = false;
		this.previous = 'offline';
		this.last = '';
	}

	call(name, result = 'number', types = [], values = []) {
		return this.player.engine.ccall(
			'mh_browser_multiplayer_' + name,
			result,
			types,
			values);
	}

	require(success) {
		if (!success)
			throw new Error(this.call('error', 'string') ||
				'Multiplayer operation failed.');
	}

	number(field, index = 0) {
		return this.call('number',
			'number',
			['string', 'number'],
			[field, index]);
	}

	text(field, index = 0) {
		return this.call(
			'text', 'string', ['string', 'number'], [field, index]);
	}

	snapshot() {
		const state = OpenMHMultiplayer.states[this.call('refresh')];
		const player = this.number('player');
		const players = [];

		// Slot zero is the dedicated server, not a playable driver.
		for (let id = 1; id < this.number('players'); id++) {
			if (!this.number('present', id))
				continue;
			players.push({
				id,
				local: id === player,
				name: this.text('name', id),
				team: this.text('team', id),
				car: this.text('car', id),
				color: this.number('color', id),
				points: this.number('points', id),
				races: this.number('races', id),
				racing: !!this.number('racing', id),
				ready: !!this.number('ready', id),
				passing: !!this.number('passing', id)
			});
		}

		const chat = Array.from(
			{length: this.number('chatCount')}, (_, index) => ({
				player: this.number('chatPlayer', index),
				color: this.number('chatColor', index),
				text: this.text('chat', index)
			}));

		const statistics =
			Array.from({length: this.number('statisticsCount')},
				     (_, index) => ({
					     name: this.text('statName', index),
					     position: this.number(
						     'statPosition', index),
					     color: this.number(
						     'statColor', index),
					     topSpeed: this.number(
						     'statSpeed', index),
					     bestLap: this.number(
						     'statBestLap', index),
					     milliseconds: this.number(
						     'statTime', index)
				     }))
				.filter(row => row.position > 0 && row.name);
		return {
			available: !!this.call('available') &&
				typeof RTCPeerConnection === 'function',
			state,
			player,
			track: this.text('track'),
			mode: this.number('mode'),
			laps: this.number('laps'),
			showFPS: !!this.number('showFPS'),
			players,
			cars: Array.from({length: this.number('carCount')},
				(_, index) => this.number('carId', index)),
			chat,
			chatRevision: this.number('chatRevision'),
			notice: this.text('notice'),
			noticeRevision: this.number('noticeRevision'),
			statistics,
			statisticsRevision: this.number('statisticsRevision'),
			resultsRevision: this.number('resultsRevision'),
			finished: !!this.number('finished'),
			error: state === 'failed' ? this.text('error') : ''
		};
	}

	string(value, label) {
		if (typeof value !== 'string' || value.includes('\0'))
			throw new Error('Invalid ' + label + '.');
		return value;
	}

	async join({
		url,
		password = '',
		iceServers = [],
		relayOnly = false,
		settings = {}
	} = {}) {
		const p = this.player;
		if (this.engaged || p.commandBusy || p.engine.saves.busy ||
			!['ready', 'finished', 'failed'].includes(p.state))
			throw new Error(
				'Leave the current race or server before connecting.');
		if (!this.snapshot().available)
			throw new Error('Browser multiplayer is unavailable.');
		if (document.hidden || p.lifecycle.pageHidden ||
			p.lifecycle.contextLost)
			throw new Error(
				'Return to the game before connecting.');
		this.string(url, 'signaling URL');
		this.string(password, 'password');
		if (!/^[\x20-\x7e]{0,15}$/.test(password) ||
			!Array.isArray(iceServers) ||
			typeof relayOnly !== 'boolean')
			throw new Error(
				'Invalid multiplayer connection settings.');
		const endpoint = new URL(url, location.href);
		if (!['http:', 'https:'].includes(endpoint.protocol) ||
			endpoint.username || endpoint.password || endpoint.hash)
			throw new Error(
				'Use an HTTP or HTTPS signaling URL without credentials or a fragment.');
		const servers = [];
		for (const server of iceServers) {
			const urls = Array.isArray(server.urls) ? server.urls
								: [server.urls];
			for (const iceURL of urls) {
				servers.push([
					this.string(iceURL, 'ICE server URL'),
					this.string(server.username ?? '',
						'ICE username'),
					this.string(server.credential ?? '',
						'ICE credential')
				]);
			}
		}
		p.commandBusy = true;
		try {
			await p.waitUntilReady();
			p.lifecycle.setPaused(false);
			p.configure(settings);
			this.require(this.call('configure',
				'number',
				['string', 'number'],
				[endpoint.href, Number(relayOnly)]));
			for (const server of servers)
				this.require(this.call('ice',
					'number',
					['string', 'string', 'string'],
					server));
			this.require(this.call(
				'join', 'number', ['string'], [password]));
			this.engaged = true;
			this.previous = 'connecting';
			this.last = '';
			p.state = 'connecting';
			return await new Promise((resolve, reject) => {
				this.pendingJoin = {resolve, reject};
			});
		} finally {
			p.commandBusy = false;
		}
	}

	profile({name, team, color} = {}) {
		if (typeof name !== 'string' ||
			!/^[\x20-\x7e]{1,15}$/.test(name))
			throw new Error(
				'Player name must contain 1 to 15 printable ASCII characters.');
		if (typeof team !== 'string' ||
			!/^[\x20-\x7e]{0,4}$/.test(team))
			throw new Error(
				'Team must contain up to 4 printable ASCII characters.');
		if (!Number.isInteger(color) || color < 0 || color > 0xffffff)
			throw new Error('Player color must be an RGB value.');
		this.require(this.call('player',
			'number',
			['string', 'string', 'number'],
			[name, team, color]));
	}

	showFPS(enabled) {
		if (typeof enabled !== 'boolean')
			throw new Error('Choose whether to show FPS.');
		this.require(this.call(
			'show_fps', 'number', ['number'], [Number(enabled)]));
	}

	chat(text) {
		this.string(text, 'chat message');
		this.require(this.call('chat', 'number', ['string'], [text]));
		return Array.from({length: this.number('commandCount')},
			(_, index) => this.text('command', index));
	}

	toggleReady() {
		this.require(this.call('toggle_ready'));
	}

	ready(ready) {
		if (typeof ready !== 'boolean')
			throw new Error('Choose ready or not ready.');
		this.require(this.call(
			'ready', 'number', ['number'], [Number(ready)]));
	}

	leave() {
		if (this.pendingLeave)
			return this.pendingLeave.promise;
		this.require(this.call('leave'));
		const pending = {};
		pending.promise =
			new Promise((resolve, reject) => Object.assign(
					    pending, {resolve, reject}));
		this.pendingLeave = pending;
		return pending.promise;
	}

	returnToLobby() {
		if (this.pendingReturn)
			return this.pendingReturn.promise;
		this.require(this.call('return'));
		const pending = {};
		pending.promise =
			new Promise((resolve, reject) => Object.assign(
					    pending, {resolve, reject}));
		this.pendingReturn = pending;
		return pending.promise;
	}

	poll() {
		if (!this.engaged)
			return false;
		const p = this.player;
		const view = this.snapshot();
		const previous = this.previous;
		this.previous = view.state;
		if (['lobby', 'loading', 'racing'].includes(view.state)) {
			this.pendingJoin?.resolve(view);
			this.pendingJoin = null;
		}
		if (view.state === 'racing') {
			p.state = 'racing';
			if (previous !== 'racing') {
				p.finishNotified = false;
				p.raceStarted();
			}
			p.notifyFinish();
		} else if (view.state === 'loading') {
			p.state = 'starting';
			if (previous !== 'loading')
				p.emit('loading', {percent: 0});
		} else if (view.state === 'lobby') {
			p.state = 'lobby';
			if (previous === 'racing' || this.pendingReturn) {
				const results = p.results();
				this.pendingReturn?.resolve(results);
				this.pendingReturn = null;
				p.emit('finished', results);
			}
		} else if (view.state === 'offline' ||
			view.state === 'failed') {
			const error = new Error(
				view.error || 'Disconnected from the server.');
			this.engaged = false;
			p.state = view.state === 'failed' ? 'failed' : 'ready';
			p.lifecycle.update();
			this.pendingJoin?.reject(error);
			this.pendingReturn?.reject(error);
			if (view.state === 'failed') {
				this.pendingLeave?.reject(error);
				p.emit('error', error);
			} else
				this.pendingLeave?.resolve();
			this.pendingJoin = this.pendingReturn =
				this.pendingLeave = null;
		} else
			p.state = view.state;
		const encoded = JSON.stringify(view);
		if (encoded !== this.last) {
			this.last = encoded;
			p.emit('multiplayer', view);
		}
		return true;
	}

	stop(error) {
		for (const key of ['pendingJoin',
			     'pendingReturn',
			     'pendingLeave']) {
			this[key]?.reject(error);
			this[key] = null;
		}
		this.engaged = false;
	}
}
