'use strict';

// Public page API. The compiled application owns validation, timing and races.
class OpenMH extends EventTarget {
	// Catalog indices follow mh_frontend_catalog in the application API.
	static catalogs = {
		cars: 0,
		tracks: 1,
		renderers: 2,
		controls: 3,
		modes: 4,
		horns: 5,
		music: 6,
		audioProfiles: 7
	};
	// Indices follow mh_control_action in the native profile format.
	static controlActions = [
		'left',
		'right',
		'accelerate',
		'brake',
		'shiftUp',
		'shiftDown',
		'horn',
		'handbrake',
		'rearView',
		'menu',
		'info',
		'chat',
		'inCar',
		'chase',
		'trackCamera',
		'mirror',
		'cycleCar',
		'screenshot'
	];
	static fields = {
		'player.color': 'player_color',
		car: 'car',
		track: 'track',
		horn: 'horn',
		laps: 'laps',
		difficulty: 'difficulty',
		automatic: 'automatic',
		catchUp: 'catch_up',
		controls: 'controls',
		'graphics.width': 'width',
		'graphics.height': 'height',
		'graphics.detail': 'detail',
		'graphics.showFPS': 'show_fps',
		'graphics.showPlayerNames': 'show_player_names',
		'sound.effectsVolume': 'effects_volume',
		'sound.musicVolume': 'music_volume',
		'sound.musicTrack': 'music_track',
		'sound.profile': 'audio_profile'
	};

	// An embedding page supplies a canvas and either a File or a hosted URL.
	static async create({canvas, assets, arguments: args = []}) {
		const engine = await createOpenMH({canvas});
		engine.storage = new OpenMHStorage(engine);
		const packages = Array.isArray(assets) ? assets : [assets];
		if (!packages.length)
			throw new Error('Choose at least one asset package.');
		engine.FS.mkdir('/assets');
		for (const [index, asset] of packages.entries()) {
			const bytes = asset instanceof Blob
				? await asset.arrayBuffer()
				: await OpenMH.download(asset);
			engine.FS.writeFile('/assets/package' + index + '.ass',
				new Uint8Array(bytes));
		}
		return OpenMH.attach(engine, {canvas, arguments: args});
	}

	static downloadLength(response) {
		// Fetch streams contain decoded bytes. A compressed Content-Length
		// measures something else; cross-origin encoding headers may be hidden.
		const encoding = response.headers.get('Content-Encoding');
		if (response.type === 'cors' ||
			(encoding && encoding !== 'identity'))
			return 0;
		const length = Number(response.headers.get('Content-Length'));
		return Number.isSafeInteger(length) && length > 0 ? length : 0;
	}

	static async downloadBlob(url, progress = () => {}) {
		const response = await fetch(url);
		if (!response.ok)
			throw new Error('Asset download failed: HTTP ' +
				response.status);

		let total = OpenMH.downloadLength(response);
		let received = 0;
		progress(0, total);
		let body = response;
		if (response.body) {
			const measured =
				response.body.pipeThrough(new TransformStream({
					transform(chunk, controller) {
						received += chunk.byteLength;
						if (received > total)
							total = 0;
						progress(received, total);
						controller.enqueue(chunk);
					}
				}));
			// Let the browser collect the Blob without retaining every chunk
			// in a JavaScript array alongside the finished package.
			body = new Response(measured);
		}
		const blob = await body.blob();
		progress(blob.size, blob.size);
		return blob;
	}

	static async download(url) {
		return (await OpenMH.downloadBlob(url)).arrayBuffer();
	}

	// Attach to a prepared runtime; the API owns callbacks and shutdown from here.
	static async attach(
		engine, {canvas, arguments: args = [], lifecycle = null}) {
		const player = new OpenMH(engine, canvas, lifecycle);
		player.require(engine._mh_browser_race_enable());
		player.bindEngineEvents();
		await player.start(args);
		return player;
	}

	bindEngineEvents() {
		const engine = this.engine;
		const previousExit = engine.onGameExit;
		engine.onGameExit = (result) => {
			this.stop(new Error('The game closed' +
				(result ? ' with an error.' : '.')));
			previousExit?.(result);
		};
		const previousStorageError = engine.onStorageError;
		engine.onStorageError = (message) => {
			previousStorageError?.(message);
			const error = new Error(message);
			this.emit('storageerror', error);
			this.pendingReady?.reject(error);
		};
		const previousAbort = engine.onAbort;
		engine.onAbort = (reason) => {
			this.stop(new Error(String(reason)));
			previousAbort?.(reason);
		};
	}

	async start(args) {
		const ready = new Promise((resolve, reject) => {
			this.pendingReady = {resolve, reject};
		});
		this.timer = setInterval(() => this.poll(), 16);
		try {
			this.engine.callMain([
				'--assets',
				'/assets',
				'--user-data',
				'/userdata',
				'--audio',
				'1',
				'--fullscreen',
				'0',
				'--grab-mouse',
				'0',
				...args
			]);
			await ready;
			this.loadRaceHistory();
			this.lifecycle.start();
			this.checkpoints.start();
			return this;
		} catch (error) {
			if (this.state !== 'closed')
				await this.destroy();
			this.stop(error);
			throw error;
		}
	}

	constructor(engine, canvas, lifecycle) {
		super();
		this.engine = engine;
		this.canvas = canvas;
		this.historyStore = new OpenMHRaceHistory(engine.FS);
		this.raceHistory = [];
		this.historyError = '';
		this.state = 'preparing';
		this.multiplayer = new OpenMHMultiplayer(this);
		this.preferences = new OpenMHPreferences(this);
		this.lifecycle = lifecycle ||
			new OpenMHLifecycle(canvas,
				(value) =>
					engine._mh_browser_set_suspended(value),
				(state) => this.emit('suspension', state));
		this.lifecycle.runHidden = () => this.multiplayer.engaged;
		this.lifecycle.onHidden = () => {
			if (!this.multiplayer.engaged &&
				this.state === 'racing' &&
				!this.lifecycle.userPaused)
				this.pause();
		};
		engine.saves ||= new OpenMHSaveSession(engine);
		this.checkpoints = engine.saves.checkpoints;
	}

	on(name, callback) {
		const listener = (event) => callback(event.detail);
		this.addEventListener(name, listener);
		return () => this.removeEventListener(name, listener);
	}

	emit(name, detail) {
		this.dispatchEvent(new CustomEvent(name, {detail}));
	}

	call(name, result = 'number', types = [], values = []) {
		return this.engine.ccall(
			'mh_browser_race_' + name, result, types, values);
	}

	require(success) {
		if (!success)
			throw new Error(this.call('error', 'string') ||
				'Race operation failed.');
	}

	readCatalog(kind) {
		const count = this.engine._mh_browser_race_count(kind);
		const entries = [];

		// Cars/courses start at 1; CD audio starts at track 2.
		const firstId = kind < 2		 ? 1
			: kind === OpenMH.catalogs.music ? 2
							 : 0;

		for (let index = 0; index < count; index++) {
			entries.push({
				id: index + firstId,
				name: this.call('choice',
					'string',
					['number', 'number'],
					[kind, index]),
				available: !!this.engine
					._mh_browser_race_available(kind, index)
			});
		}

		return entries;
	}

	readCarRatings(id) {
		const values = [0, 1, 2].map(
			(field) => this.engine._mh_browser_race_car_rating(
				id, field));

		if (values[0] < 0)
			return null;

		return {
			speed: values[0],
			acceleration: values[1],
			handling: values[2]
		};
	}

	readTrackDetails(id) {
		const [location, length, lengthUS] =
			[0, 1, 2].map((field) => this.call('track_detail',
					      'string',
					      ['number', 'number'],
					      [id, field]));

		const musicTrack = this.engine._mh_browser_race_track_music(id);
		return {location, length, lengthUS, musicTrack};
	}

	readResolutions(id) {
		const count = this.engine._mh_browser_race_resolution_count(id);
		const modes = [];

		for (let index = 0; index < count; index++) {
			modes.push({
				width: this.engine._mh_browser_race_resolution(
					id, index, 0),
				height: this.engine._mh_browser_race_resolution(
					id, index, 1)
			});
		}

		return modes;
	}

	readNameColors() {
		const colors = [];
		for (let index = 0;; index++) {
			const color =
				this.engine._mh_browser_race_name_color(index);
			if (color < 0)
				return colors;
			colors.push(color);
		}
	}

	getContent() {
		const content = {nameColors: this.readNameColors()};

		for (const [name, kind] of Object.entries(OpenMH.catalogs))
			content[name] = this.readCatalog(kind);

		for (const car of content.cars)
			car.ratings = this.readCarRatings(car.id);

		for (const track of content.tracks)
			Object.assign(track, this.readTrackDetails(track.id));

		for (const renderer of content.renderers)
			renderer.resolutions =
				this.readResolutions(renderer.id);

		return content;
	}

	getControlBindings(id) {
		this.validateControlProfile(id);
		return Object.fromEntries(OpenMH.controlActions.map(
			(name, action) => [name,
				this.call('control_key',
					'string',
					['number', 'number'],
					[id, action]) ||
					null]));
	}

	validateControlProfile(id) {
		if (!Number.isInteger(id) || id < 0 ||
			id >= this.engine._mh_browser_race_count(
				      OpenMH.catalogs.controls))
			throw new Error('Unknown control profile.');
	}

	setControlBinding(id, action, key) {
		this.validateControlProfile(id);
		const index = OpenMH.controlActions.indexOf(action);
		if (index < 0 || typeof key !== 'string' || !key ||
			key.includes('\0'))
			throw new Error('Invalid keyboard binding.');
		this.require(this.call('bind_key',
			'number',
			['number', 'number', 'string'],
			[id, index, key]));
	}

	getTrackMap(id) {
		if (!Number.isInteger(id) || id < 1 ||
			id > this.engine._mh_browser_race_count(1))
			throw new Error('Unknown track.');
		this.require(this.engine._mh_browser_race_map_load(id));
		const width = this.engine._mh_browser_race_map_size(0),
		      height = this.engine._mh_browser_race_map_size(1);
		if (!width || !height)
			return null;
		const pointer = this.engine._mh_browser_race_map_pixels();
		// Own a copy: loading another map or growing Wasm memory must not invalidate it.
		const pixels = new Uint8ClampedArray(this.engine.HEAPU8.slice(
			pointer, pointer + width * height * 4));
		return new ImageData(pixels, width, height);
	}

	getPreferenceSchema() {
		return this.preferences.schema();
	}

	getPreferences() {
		return this.preferences.read();
	}

	updatePreferences(values) {
		return this.preferences.update(values);
	}

	getControlProfile(id) {
		return this.preferences.control(id);
	}

	updateControlProfile(id, values) {
		return this.preferences.updateControl(id, values);
	}

	getSettings() {
		this.require(this.engine._mh_browser_race_reset());
		const settings = {
			player: {
				name: this.call('player', 'string'),
				team: this.call('team', 'string')
			},
			graphics: {},
			sound: {}
		};
		for (const [path, key] of Object.entries(OpenMH.fields)) {
			const parts = path.split('.');
			const owner = parts.length === 1 ? settings
							 : settings[parts[0]];
			owner[parts.at(-1)] =
				this.call('get', 'number', ['string'], [key]);
		}
		settings.mode =
			this.call('get', 'number', ['string'], ['mode']);
		settings.graphics.renderer =
			this.call('get', 'number', ['string'], ['renderer']);
		settings.sound.musicTrack =
			this.engine._mh_browser_race_track_music(
				settings.track);
		return settings;
	}

	setNumber(key, value) {
		if (typeof value === 'boolean')
			value = Number(value);
		if (!Number.isSafeInteger(value) || value < 0 ||
			value > 0xffffffff)
			throw new Error('Invalid value for ' + key);
		this.require(this.call(
			'set', 'number', ['string', 'number'], [key, value]));
	}

	setPlayerName(value) {
		if (typeof value !== 'string' ||
			!/^[\x20-\x7e]{1,15}$/.test(value))
			throw new Error(
				'Player name must contain 1 to 15 printable ASCII characters.');
		this.require(this.call('name', 'number', ['string'], [value]));
	}

	setPlayerTeam(value) {
		if (typeof value !== 'string' ||
			!/^[\x20-\x7e]{0,4}$/.test(value))
			throw new Error(
				'Team must contain up to 4 printable ASCII characters.');
		this.require(
			this.call('set_team', 'number', ['string'], [value]));
	}

	setRenderer(value) {
		const choice =
			this.readCatalog(OpenMH.catalogs.renderers)
				.find((item) => item.id === value ||
						item.name.toLowerCase() ===
							String(value)
								.toLowerCase());
		if (!choice)
			throw new Error('Unknown renderer.');
		this.setNumber('renderer', choice.id);
	}

	setSetting(path, value) {
		if (path === 'player.name')
			this.setPlayerName(value);
		else if (path === 'player.team')
			this.setPlayerTeam(value);
		else if (path === 'graphics.renderer')
			this.setRenderer(value);
		else if (path === 'mode')
			this.setNumber('mode',
				value === 'quickRace'		 ? 0
					: value === 'timeAttack' ? 2
								 : value);
		else if (Object.hasOwn(OpenMH.fields, path))
			this.setNumber(OpenMH.fields[path], value);
		else
			throw new Error('Unknown race setting: ' + path);
	}

	configureGroup(group, settings) {
		if (!settings || typeof settings !== 'object' ||
			Array.isArray(settings))
			throw new Error('Invalid ' + group + ' settings.');
		for (const [key, value] of Object.entries(settings))
			this.setSetting(group + '.' + key, value);
	}

	configure(config) {
		this.require(this.engine._mh_browser_race_reset());
		for (const [key, value] of Object.entries(config)) {
			if (['player', 'graphics', 'sound'].includes(key))
				this.configureGroup(key, value);
			else
				this.setSetting(key, value);
		}
	}

	async waitUntilReady() {
		while (this.engine._mh_browser_race_state() === 0) {
			if (this.state === 'closed')
				throw new Error('Game closed.');
			await new Promise((resolve) => setTimeout(resolve, 16));
		}
	}

	async startRace(config = {}) {
		if (!['ready', 'finished', 'failed'].includes(this.state) ||
			this.commandBusy || this.engine.saves.busy)
			throw new Error(
				'A race is already starting or running.');
		this.commandBusy = true;
		try {
			await this.waitUntilReady();
			this.configure(config);
			this.require(this.engine._mh_browser_race_start());
			this.finishNotified = false;
			this.state = 'starting';
			this.emit('loading', {percent: 0});
			return await new Promise((resolve, reject) => {
				this.pendingStart = {resolve, reject};
			});
		} finally {
			this.commandBusy = false;
		}
	}

	getMultiplayer() {
		return this.multiplayer.snapshot();
	}

	joinServer(options) {
		return this.multiplayer.join(options);
	}

	setMultiplayerProfile(profile) {
		this.multiplayer.profile(profile);
	}

	setMultiplayerShowFPS(enabled) {
		this.multiplayer.showFPS(enabled);
	}

	sendChat(text) {
		return this.multiplayer.chat(text);
	}

	toggleMultiplayerReady() {
		this.multiplayer.toggleReady();
	}

	requestReady(ready) {
		this.multiplayer.ready(ready);
	}

	leaveServer() {
		return this.multiplayer.leave();
	}

	returnToLobby() {
		return this.multiplayer.returnToLobby();
	}

	isPaused() {
		return this.lifecycle.userPaused;
	}

	pause() {
		if (this.multiplayer.engaged)
			throw new Error(
				'Online races cannot be paused. Return to the lobby or leave the server.');
		if (this.state !== 'racing')
			throw new Error('No active race.');
		this.lifecycle.setPaused(true);
		this.emit('pause', true);
	}

	resume() {
		if (this.state !== 'racing')
			throw new Error('No active race.');
		this.lifecycle.setPaused(false);
		this.canvas.focus({preventScroll: true});
		this.emit('pause', false);
	}

	async leaveRace() {
		if (this.multiplayer.engaged)
			return this.returnToLobby();
		if (this.pendingLeave)
			throw new Error('Already leaving the race.');
		this.require(this.engine._mh_browser_race_leave());
		this.resume();
		return new Promise((resolve, reject) => {
			this.pendingLeave = {resolve, reject};
		});
	}

	retryStorage() {
		this.require(this.engine._mh_browser_retry_storage());
		this.checkpoints.retry();
	}

	// Return caller-owned data; persistence remains owned by this player.
	getRaceHistory() {
		return structuredClone(
			{races: this.raceHistory, error: this.historyError});
	}

	loadRaceHistory() {
		try {
			this.raceHistory = this.historyStore.read();
			this.historyError = '';
		} catch (error) {
			this.historyError = error.message;
		}
	}

	recordRaceHistory(results) {
		if (!results.length)
			return;
		const race = OpenMHRaceHistory.describe(
			this.getSettings(), this.getContent(), results);
		try {
			this.raceHistory = this.historyStore.append(race);
			this.historyError = '';
			this.checkpoints.request();
		} catch (error) {
			// Keep the latest result visible without replacing malformed saved history.
			this.raceHistory = [race, ...this.raceHistory].slice(
				0, OpenMHRaceHistory.limit);
			this.historyError =
				'Could not save race history. ' + error.message;
		}
	}

	results() {
		return Array.from(
			{length: this.engine._mh_browser_race_result(0, 0)},
			(_, index) => ({
				player: index === 0,
				position: this.engine._mh_browser_race_result(
					index, 1),
				finished: !!this.engine._mh_browser_race_result(
					index, 2),
				milliseconds:
					this.engine._mh_browser_race_result(
						index, 3),
				bestLap: this.engine._mh_browser_race_result(
					index, 4),
				estimated:
					!!this.engine._mh_browser_race_result(
						index, 5)
			}));
	}

	poll() {
		if (this.multiplayer.poll())
			return;
		const next = [
			'preparing',
			'ready',
			'starting',
			'racing',
			'finished',
			'failed'
		][this.engine._mh_browser_race_state()];
		if (next === 'racing')
			this.notifyFinish();
		// Storage commits may temporarily hide a state. Publish only durable returns.
		if (!next || next === 'preparing' || next === this.state)
			return;
		this.state = next;
		switch (next) {
		case 'ready':
			this.becameReady();
			break;
		case 'racing':
			this.raceStarted();
			break;
		case 'finished':
			this.raceFinished();
			break;
		case 'failed':
			this.raceFailed();
			break;
		}
	}

	notifyFinish() {
		if (this.finishNotified ||
			!this.engine._mh_browser_race_completed())
			return;

		this.finishNotified = true;
		this.emit('finishline');
	}

	becameReady() {
		this.pendingReady?.resolve();
		this.pendingReady = null;
	}

	raceStarted() {
		this.emit('loading', {percent: 100});
		this.pendingStart?.resolve();
		this.pendingStart = null;
		this.emit('started');
	}

	raceFinished() {
		const results = this.results();
		this.recordRaceHistory(results);
		this.pendingLeave?.resolve(results);
		this.pendingLeave = null;
		this.emit('finished', results);
	}

	raceFailed() {
		const error = new Error(this.call('error', 'string'));
		this.pendingStart?.reject(error);
		this.pendingLeave?.reject(error);
		this.pendingStart = this.pendingLeave = null;
		this.emit('error', error);
	}

	async destroy() {
		this.engine._mh_browser_close();
		await this.engine.storage.operation;
	}

	stop(error) {
		this.multiplayer.stop(error);
		clearInterval(this.timer);
		this.lifecycle.stop();
		this.engine.saves.stop();
		if (this.engine.storage.status === 1)
			this.engine.storage.cancel();
		else
			this.engine.storage.close();
		for (const key of ['pendingReady',
			     'pendingStart',
			     'pendingLeave']) {
			this[key]?.reject(error);
			this[key] = null;
		}
		this.state = 'closed';
	}
}
