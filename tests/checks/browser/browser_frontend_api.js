'use strict';

const canvas = document.getElementById('canvas');
canvas.id = 'embedded:game';
const decoy = document.createElement('canvas');
decoy.id = 'canvas';
decoy.width = decoy.height = 9;
decoy.tabIndex = 0;
document.body.append(decoy);
const error = document.getElementById('error');
let game;
let lastError = '';

async function preferencesWithoutRace(player) {
	const schema = player.getPreferenceSchema();
	check(schema.find((field) => field.key === 'graphics.viewDistance')
				.maximum === 150,
		'preference descriptors expose application limits');
	check(!schema.find((field) => field.key === 'graphics.vsync').available,
		'browser presentation controls are explicitly unavailable');
	check(schema.find((field) => field.key === 'graphics.namePlates')
				.choices.length === 3,
		'nameplate styles are available independently of visibility');
	await player.updatePreferences({
		laps: 7,
		'player.units': 1,
		'player.customCarColors': true,
		'player.carColor1': 0x123456,
		'player.chatShortcut0': 'Hello racers',
		'graphics.smoke': false,
		'graphics.detail': 3,
		'graphics.namePlates': 1,
		'graphics.brightnessRed': 0.8,
		'graphics.checkpointDelay': 1250,
		'sound.loop': false
	});
	const saved = player.getPreferences();
	check(saved['graphics.detail'] === 4 && saved['graphics.smoke'] === 0,
		'custom detail overrides follow presets regardless of property order');
	check(player.getSettings().graphics.detail === 4 &&
			player.getSettings().graphics.showPlayerNames === 1,
		'race defaults share the preference values');
	for (const patch of [{'player.units': 0, 'graphics.viewDistance': 151},
		     {'player.units': 0, 'graphics.vsync': false},
		     {'player.units': 0, 'graphics.fullscreen': true},
		     {'player.units': 0, 'player.name': 'bad;name'},
		     {'graphics.width': NaN},
		     {'graphics.width': 640.5},
		     {'graphics.width': Infinity},
		     {'graphics.unknown': 1}]) {
		let rejected = false;
		try {
			await player.updatePreferences(patch);
		} catch {
			rejected = true;
		}
		check(rejected &&
				JSON.stringify(saved) ===
					JSON.stringify(player.getPreferences()),
			'invalid or unsupported preferences leave the full batch unchanged: ' +
				Object.keys(patch).join(', '));
	}
	const id = player.getSettings().controls;
	await player.updateControlProfile(
		id, {mouseSpeed: [0.75, 1.25, 1], bindings: {accelerate: 'W'}});
	const control = player.getControlProfile(id);
	check(control.mouseSpeed[0] === 0.75 &&
			control.bindings.accelerate === 'W',
		'controls can be updated and saved outside a race');
	check(!control.capabilities.actions.menu &&
			!control.capabilities.actions.screenshot,
		'host-owned and unavailable actions are reported');
	let rejected = false;
	try {
		await player.updateControlProfile(
			id, {mouseSpeed: [2, 2, 2], bindings: {menu: 'W'}});
	} catch {
		rejected = true;
	}
	check(rejected && player.getControlProfile(id).mouseSpeed[0] === 0.75,
		'a rejected control binding does not publish the mouse speed changes');
}

function checkReloadedPreferences(player) {
	const saved = player.getPreferences();
	check(saved.laps === 7 && player.getSettings().laps === 7,
		'seven laps remain selected after reopening the player');
	check(saved['player.units'] === 1 &&
			saved['player.carColor1'] === 0x123456 &&
			saved['player.chatShortcut0'] === 'Hello racers' &&
			saved['sound.loop'] === 0,
		'personal and sound preferences survive reopening without a race');
	check(saved['graphics.detail'] === 4 && saved['graphics.smoke'] === 0 &&
			saved['graphics.namePlates'] === 1 &&
			saved['graphics.checkpointDelay'] === 1250 &&
			Math.abs(saved['graphics.brightnessRed'] - 0.8) <
				0.0001,
		'custom graphics preferences survive reopening without a race');
	const control = player.getControlProfile(player.getSettings().controls);
	check(control.mouseSpeed[0] === 0.75 &&
			control.bindings.accelerate === 'W',
		'control preferences survive reopening without a race');
}

function raceMode(player) {
	return player.getContent().modes.some(
		       (mode) => mode.available && mode.id === 2)
		? 'timeAttack'
		: 'quickRace';
}

async function raceAndLeave(player) {
	const content = player.getContent();
	const track = content.tracks.find((item) => item.available);
	const car = content.cars.find((item) => item.available);
	check(content.renderers.some(
		      (item) => item.available && item.name === 'OpenMH'),
		'API exposes supported renderers');
	check(content.audioProfiles.some((profile) => profile.available),
		'API exposes included audio profiles');
	const defaults = player.getSettings();
	check(content.renderers.some((item) => item.available &&
			      item.id === defaults.graphics.renderer) &&
			content.audioProfiles.some((item) => item.available &&
					item.id === defaults.sound.profile),
		'saved defaults use included profiles');
	for (const item of content.audioProfiles.filter(
		     (item) => !item.available)) {
		let rejected = false;
		try {
			await player.startRace({sound: {profile: item.id}});
		} catch {
			rejected = true;
		}
		check(rejected && player.state === 'ready',
			'excluded audio profile is rejected before race startup');
	}
	for (const item of content.renderers.filter(
		     (item) => !item.available)) {
		let rejected = false;
		try {
			await player.startRace({graphics: {renderer: item.id}});
		} catch {
			rejected = true;
		}
		check(rejected && player.state === 'ready',
			'excluded renderer is rejected before race startup');
	}
	let loading = 0, started = 0, finished = 0;
	player.on('loading', () => loading++);
	player.on('started', () => started++);
	player.on('finished', (rows) => {
		finished++;
		check(player.getRaceHistory().races.length === 1,
			'history is recorded before the standalone finished event');
		rows[0].position = 99;
	});
	await player.startRace({
		player: {name: 'API Racer', team: 'OPEN', color: 0x123456},
		car: car.id,
		track: track.id,
		mode: raceMode(player),
		graphics: {renderer: 'openmh', width: 640, height: 480},
		sound: {effectsVolume: 0, musicVolume: 0}
	});
	await waitFor(() => game.flow?.frames > 10, 'standalone API renders');
	check(started === 1 && loading >= 2,
		'loading and started events accompany successful launch');
	check(canvas.width > 9 && decoy.width === 9,
		'the supplied canvas receives presentation while an unrelated #canvas is untouched: ' +
			canvas.width + '/' + decoy.width);
	const key = {
		key: 'ArrowUp',
		code: 'ArrowUp',
		keyCode: 38,
		which: 38,
		bubbles: true,
		cancelable: true
	};
	canvas.focus();
	canvas.dispatchEvent(new KeyboardEvent('keydown', key));
	await waitFor(() => game.flow.held > 0,
		'embedded canvas receives keyboard input');
	canvas.dispatchEvent(new KeyboardEvent('keyup', key));
	await waitFor(() => game.flow.held === 0, 'embedded key release');
	check(true, 'keyboard input follows the supplied canvas');
	decoy.dispatchEvent(new KeyboardEvent('keydown', key));
	await sleep(100);
	check(game.flow.held === 0,
		'an unrelated canvas does not feed the game keyboard');
	decoy.dispatchEvent(new KeyboardEvent('keyup', key));
	player.pause();
	await game.saves.exportSnapshot();
	check(game.suspension.suspended === 1,
		'backup preserves an existing user pause');
	const write = game.storage.database.write.bind(game.storage.database);
	let release;
	const gate = new Promise((resolve) => {
		release = resolve;
	});
	game.storage.database.write = async (snapshot) => {
		await gate;
		return write(snapshot);
	};
	game.FS.writeFile('/userdata/backup-probe', 'latest');
	const exporting = game.saves.exportSnapshot();
	await waitFor(() => game.storage.status === 1,
		'backup starts its fresh checkpoint');
	player.resume();
	const frames = game.flow.frames;
	await sleep(100);
	check(game.suspension.suspended === 1 && game.flow.frames === frames,
		'user resume cannot advance a race while a backup owns its state');
	release();
	await exporting;
	game.storage.database.write = write;
	check(game.suspension.suspended === 0 && game.saves.checkpoints.active,
		'backup restores the latest requested suspension and checkpoint state');
	await player.leaveRace();
	const history = player.getRaceHistory();
	check(history.error === '' && history.races[0].driver === 'API Racer' &&
			history.races[0].rows[0].position !== 99,
		'standalone races record history without a results widget or borrowed result rows');
	history.races[0].driver = 'Mutated';
	history.races[0].rows[0].position = 99;
	check(player.getRaceHistory().races[0].driver === 'API Racer' &&
			player.getRaceHistory().races[0].rows[0].position !==
				99,
		'history snapshots belong to the caller');
	const backup = await game.saves.exportSnapshot();
	const saved = backup.entries.find(
		(entry) => entry.path === 'racehistory.json');
	check(JSON.parse(new TextDecoder().decode(saved.data)).races.length ===
			1,
		'standalone history is included in save exports');
	check(finished === 1 && player.results().length > 0,
		'standalone API delivers a single finish event and results');
}

async function reopenSavedPlayer() {
	canvas.removeAttribute('id');
	// A fresh instance can acquire the same origin's save lock after teardown.
	const session = await OpenMHSession.create({canvas});
	const response = await fetch('/game.ass');
	await session.mount([await response.blob()]);
	const next = await session.start(
		{arguments: ['--audio', '0', '--unlock-all']});
	check(next.state === 'ready' &&
			next.getSettings().player.name === 'API Racer' &&
			next.getSettings().player.team === 'OPEN' &&
			next.getSettings().player.color === 0x123456,
		'fresh instance opens saved settings after the first is destroyed');
	check(next.getRaceHistory().races.length === 1 &&
			next.getRaceHistory().races[0].driver === 'API Racer',
		'standalone history survives teardown and recreation');
	await next.startRace({
		player: {team: ''},
		mode: raceMode(next),
		sound: {effectsVolume: 0, musicVolume: 0}
	});
	await next.leaveRace();
	check(next.getSettings().player.team === '',
		'team can be cleared without changing the driver name');
	check(next.getSettings().player.name === 'API Racer' &&
			next.getSettings().player.color === 0x123456,
		'omitted driver name and custom color retain their saved values');
	check(next.getRaceHistory().races.length === 2,
		'repeated standalone races append exactly one history entry each');
	const historyPath = '/userdata/racehistory.json';
	const savedHistory = next.engine.FS.readFile(historyPath);
	next.engine.FS.writeFile(historyPath, '{broken');
	await next.startRace({mode: raceMode(next)});
	await next.leaveRace();
	check(next.state === 'finished' && next.getRaceHistory().error &&
			next.getRaceHistory().races.length === 3,
		'history failure keeps the current result visible and the player usable');
	check(next.engine.FS.readFile(historyPath, {encoding: 'utf8'}) ===
			'{broken',
		'recording a race preserves malformed imported history');
	next.engine.FS.writeFile(historyPath, savedHistory);
	const snapshot = await session.exportSnapshot();
	const write = next.engine.storage.database.write.bind(
		next.engine.storage.database);
	next.engine.storage.database.write = async () => {
		throw new Error('import denied');
	};
	let rejected = false;
	try {
		await session.replaceSnapshotAndClose(snapshot);
	} catch {
		rejected = true;
	}
	check(rejected && next.state === 'finished' && next.checkpoints.active,
		'failed import leaves the live application usable');
	next.engine.storage.database.write = write;
	next.engine.FS.writeFile('/userdata/stale-after-backup', 'discard');
	next.checkpoints.request();
	await session.replaceSnapshotAndClose(snapshot);
	check(next.state === 'closed' && !next.checkpoints.active,
		'durable import closes the application and stops all checkpoint callbacks');
	const saved = await next.engine.storage.database.read();
	check(!saved.entries.some(
		      (entry) => entry.path === 'stale-after-backup'),
		'an application checkpoint cannot overwrite the imported snapshot');
	next.engine.storage.close();
}

async function resetSavedPreferences() {
	const asset =
		new File([await (await fetch('/game.ass')).blob()], 'demo.ass');
	await OpenMHPackageDatabase.validate([asset]);
	for (const invalid of [[new File(['invalid'], 'game.ass')],
		     [asset, new File([asset], 'second.ass')]]) {
		let reason = '';
		try {
			await OpenMHPackageDatabase.validate(invalid);
		} catch (error) {
			reason = error.message;
		}
		check(!!reason,
			'package validator rejects ' + invalid.at(-1).name);
	}
	const open = async () => {
		const session = await OpenMHSession.create({canvas});
		let rejected = false;
		try {
			await session.mount([
				asset,
				{
					arrayBuffer() {
						throw new Error(
							'Interrupted read');
					}
				}
			]);
		} catch {
			rejected = true;
		}
		check(rejected &&
				!session.engine.FS.readdir('/').includes(
					'assets'),
			'failed package reads roll back partial mounts');
		await session.mount([asset]);
		const player = await session.start(
			{arguments: ['--audio', '0', '--unlock-all']});
		return {session, player};
	};
	const first = await open();
	await first.player.updatePreferences(
		{'player.name': 'ResetTest', 'graphics.showFPS': true});
	await first.player.updateControlProfile(
		first.player.getSettings().controls,
		{bindings: {accelerate: 'W'}});
	first.player.engine.FS.writeFile(
		'/userdata/reset-progress.flg', new Uint8Array([8, 9]));
	const history = first.player.getRaceHistory().races.length;
	await first.session.resetData(['settings', 'controls']);
	check(first.player.state === 'closed',
		'selective reset closes the actual engine after durable replacement');
	const second = await open();
	check(second.player.getSettings().player.name !== 'ResetTest' &&
			!second.player.getSettings().graphics.showFPS,
		'restarting after reset uses native configuration defaults');
	check(second.player.getControlBindings(
				   second.player.getSettings().controls)
				.accelerate !== 'W',
		'restarting after reset restores the installed key bindings');
	check(String(second.player.engine.FS.readFile(
		      '/userdata/reset-progress.flg')) === '8,9' &&
			second.player.getRaceHistory().races.length === history,
		'configuration reset retains progress and race history across actual engine restarts');
	await second.player.destroy();
}

async function run() {
	canvas.hidden = false;
	let player = await OpenMH.create({
		canvas,
		assets: '/game.ass',
		arguments: ['--audio', '0', '--unlock-all']
	});
	game = player.engine;
	check(player.state === 'ready',
		'standalone create downloads the hosted package and becomes ready');
	check(!game.flow && !game.movie,
		'standalone API opens no original screens');
	await preferencesWithoutRace(player);
	await player.destroy();
	player = await OpenMH.create({
		canvas,
		assets: '/game.ass',
		arguments: ['--audio', '0', '--unlock-all']
	});
	game = player.engine;
	checkReloadedPreferences(player);
	// Restore the driving key used by this existing input scenario.
	await player.updateControlProfile(player.getSettings().controls,
		{bindings: {accelerate: 'UpArrow'}});
	await raceAndLeave(player);
	await player.destroy();
	check(player.state === 'closed',
		'destroy closes the engine and releases storage');
	await reopenSavedPlayer();
	await resetSavedPreferences();
	await fetch('/report',
		{method: 'POST', body: JSON.stringify({passed: true, checks})});
}

run().catch((reason) => fetch('/report', {
	method: 'POST',
	body: JSON.stringify(
		{passed: false, checks, error: reason.stack || String(reason)})
}));
