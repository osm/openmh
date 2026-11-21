const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

(async function() {
	let phase = 0, available = 1, completed = 0, showFPS = 0, replies = [];
	const calls = [], events = [];
	const context = vm.createContext({
		EventTarget,
		Event,
		URL,
		Promise,
		JSON,
		CustomEvent: class extends Event{
			constructor(name, options) {
				super(name);
				this.detail = options.detail;
			}
		},
		RTCPeerConnection: class {},
		document: {hidden: false},
		location: {href: 'https://play.invalid/'},
		OpenMHRaceHistory: class {},
		OpenMHLifecycle: class {},
		OpenMHSaveSession: class {}
	});
	for (const file of ['browser_multiplayer.js',
		     'browser_preferences.js',
		     'browser_player.js'])
		vm.runInContext(
			fs.readFileSync(
				path.join(process.argv[2], file), 'utf8'),
			context);
	const Player = vm.runInContext('OpenMH', context);
	const engine = {
		FS: {},
		saves: {busy: false, checkpoints: {}},
		_mh_browser_race_state: () => 1,
		_mh_browser_race_completed: () => completed,
		_mh_browser_race_result: (_, field) => field === 0 ? 1 : 0,
		ccall(name, type, types, values) {
			const command =
				name.replace('mh_browser_multiplayer_', '');
			if (command === 'available')
				return available;
			if (command === 'refresh')
				return phase;
			if (command === 'number') {
				const [field] = values;
				return {
					commandCount: replies.length,
					players: 2,
					carCount: 2,
					carId: [3, 1][values[1]],
					player: 1,
					present: 1,
					laps: 3,
					showFPS,
					ready: 1,
					chatCount: 1,
					statisticsCount: 0,
					finished: completed
				}[field] ??
					0;
			}
			if (command === 'text') {
				return {
					command: replies[values[1]],
					name: 'Driver',
					chat: 'Driver: hi',
					track: 'Fixture',
					error: 'Connection lost'
				}[values[0]] ||
					'';
			}
			if (command === 'error')
				return 'Operation failed';
			calls.push({command, values});
			if (command === 'show_fps') {
				if (phase !== 2)
					return 0;
				showFPS = values[0];
			}
			if (command === 'toggle_ready' && phase !== 2)
				return 0;
			if (command === 'player' && phase !== 2)
				return 0;
			if (command === 'chat') {
				if (phase !== 2)
					return 0;
				replies = values[0] === '/name Driver'
					? ['Name changed to: Driver']
					: [];
			}
			if (command === 'join')
				phase = 1;
			if (command === 'leave')
				phase = 5;
			return 1;
		}
	};
	let lifecycleUpdates = 0;
	const lifecycle = {
		setPaused: () => {},
		userPaused: false,
		update: () => lifecycleUpdates++
	};
	const player = new Player(engine, {}, lifecycle);
	player.state = 'ready';
	for (const enabled of [false, true]) {
		player.setSetting('graphics.showPlayerNames', enabled);
		assert.equal(calls.at(-1).command, 'mh_browser_race_set');
		assert.deepEqual([...calls.at(-1).values],
			['show_player_names', Number(enabled)]);
	}
	player.configure = settings =>
		calls.push({command: 'settings', settings});
	player.recordRaceHistory = () => {
		throw new Error(
			'Online races must not use single-player history');
	};
	for (const event of ['multiplayer',
		     'started',
		     'finished',
		     'finishline',
		     'error'])
		player.on(event, value => events.push({event, value}));
	const tick = async () => {
		await Promise.resolve();
		await Promise.resolve();
	};

	available = 0;
	await assert.rejects(player.joinServer({url: '/'}), /unavailable/);
	available = 1;
	await assert.rejects(player.joinServer({url: 'file:///'}), /HTTP/);
	await assert.rejects(
		player.joinServer({url: '/', password: 'bad\0'}), /password/);
	context.document.hidden = true;
	await assert.rejects(
		player.joinServer({url: '/'}), /Return to the game/);
	context.document.hidden = false;

	const canceled = player.joinServer({url: '/'});
	const canceledCheck = assert.rejects(canceled, /Disconnected/);
	await tick();
	player.poll();
	assert.equal(player.state, 'connecting');
	const leftEarly = player.leaveServer();
	phase = 0;
	player.poll();
	await leftEarly;
	await canceledCheck;
	assert.equal(player.state, 'ready');
	assert.equal(lifecycle.runHidden(), false);
	assert.ok(lifecycleUpdates > 0);
	assert.equal(player.commandBusy, false);

	const joined = player.joinServer({
		url: '/',
		settings: {car: 3},
		iceServers: [{
			urls: ['stun:test', 'turn:test'],
			username: 'user',
			credential: 'password'
		}]
	});
	await tick();
	assert.equal(calls.filter(call => call.command === 'ice').length, 2);
	assert.equal(calls.find(call => call.command === 'configure').values[0],
		'https://play.invalid/');
	phase = 2;
	player.poll();
	const lobby = await joined;
	assert.equal(player.state, 'lobby');
	assert.equal(lobby.players[0].name, 'Driver');
	assert.deepEqual(Array.from(lobby.cars), [3, 1]);
	lobby.cars[0] = 99;
	assert.equal(player.getMultiplayer().cars[0], 3);
	lobby.players[0].name = 'mutated caller copy';
	assert.equal(player.getMultiplayer().players[0].name, 'Driver');
	assert.deepEqual(Array.from(player.sendChat('/name Driver')),
		['Name changed to: Driver']);
	assert.deepEqual(Array.from(player.sendChat('hello')), []);
	const profile = {name: 'Driver Name', team: '', color: 0x123456};
	player.setMultiplayerProfile(profile);
	assert.deepEqual(
		Array.from(
			calls.find(call => call.command === 'player').values),
		['Driver Name', '', 0x123456]);
	for (const invalid of [{name: ''},
		     {name: '1234567890123456'},
		     {name: 'bad\0name'},
		     {name: 'é'},
		     {team: '12345'},
		     {team: '\n'},
		     {color: -1},
		     {color: 0x1000000},
		     {color: 1.5},
		     {color: NaN},
		     {color: 'red'}])
		assert.throws(() => player.setMultiplayerProfile(
				      {...profile, ...invalid}));
	player.setMultiplayerShowFPS(true);
	assert.equal(player.getMultiplayer().showFPS, true);
	player.setMultiplayerShowFPS(false);
	assert.equal(player.getMultiplayer().showFPS, false);
	assert.throws(() => player.setMultiplayerShowFPS('yes'), /show FPS/);
	player.toggleMultiplayerReady();
	assert.equal(calls.at(-1).command, 'toggle_ready');
	player.requestReady(true);
	assert.throws(() => player.requestReady('yes'), /Choose ready/);
	assert.throws(() => player.sendChat('bad\0text'), /chat/);
	assert.throws(() => player.pause(), /cannot be paused/);
	const notifications = events.length;
	player.poll();
	assert.equal(events.length, notifications);

	phase = 3;
	assert.throws(
		() => player.toggleMultiplayerReady(), /Operation failed/);
	assert.throws(
		() => player.setMultiplayerShowFPS(true), /Operation failed/);
	assert.throws(() => player.setMultiplayerProfile(profile),
		/Operation failed/);
	assert.throws(
		() => player.sendChat('/name Racing'), /Operation failed/);
	player.poll();
	assert.equal(player.state, 'starting');
	phase = 4;
	player.poll();
	assert.equal(player.state, 'racing');
	assert.equal(events.filter(e => e.event === 'started').length, 1);
	// Visibility handling must not attempt a local pause in an online race.
	assert.equal(lifecycle.runHidden(), true);
	lifecycle.onHidden();
	completed = 1;
	player.poll();
	player.poll();
	assert.equal(events.filter(e => e.event === 'finishline').length, 1);
	const returned = player.leaveRace();
	phase = 2;
	player.poll();
	await returned;
	assert.equal(player.state, 'lobby');
	assert.equal(events.filter(e => e.event === 'finished').length, 1);

	const leaving = player.leaveServer();
	player.poll();
	assert.equal(player.state, 'leaving');
	phase = 0;
	player.poll();
	await leaving;
	assert.equal(player.state, 'ready');
	assert.equal(lifecycle.runHidden(), false);
	assert.ok(lifecycleUpdates > 0);

	const failed = player.joinServer({url: '/'});
	const failureCheck = assert.rejects(failed, /Connection lost/);
	await tick();
	phase = 6;
	player.poll();
	await failureCheck;
	assert.equal(player.state, 'failed');
	assert.equal(events.filter(e => e.event === 'error').length, 1);

	const interrupted = player.joinServer({url: '/'});
	const interruptedCheck = assert.rejects(interrupted, /closed/);
	await tick();
	player.multiplayer.stop(new Error('closed'));
	await interruptedCheck;
	assert.equal(player.commandBusy, false);
	console.log('multiplayer player API passed');
})().catch(error => {
	console.error(error);
	process.exitCode = 1;
});
