'use strict';

const checks = [];

function check(condition, message) {
	if (!condition)
		throw new Error(message);
	checks.push(message);
}

async function rejected(operation, message) {
	let failed = false;
	try {
		await operation();
	} catch {
		failed = true;
	}
	check(failed, message);
}

async function settle(storage) {
	await storage.operation;
	return storage.status;
}

async function synchronize(storage, prepare = false) {
	storage.begin(prepare);
	if ((await settle(storage)) !== 0)
		throw new Error(storage.error);
}

async function instance() {
	const game = await createOpenMH({});
	return new OpenMHStorage(game);
}

function databaseResult(request) {
	return new Promise((resolve, reject) => {
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error);
	});
}

async function failedSaveOpen() {
	const name = 'openmotor-user-data';
	const newer = await databaseResult(indexedDB.open(name, 2));
	newer.close();

	const first = await instance();
	const second = await instance();
	try {
		await rejected(() => first.exportSnapshot(),
			'export rejects an unsupported save database version');
		await databaseResult(indexedDB.deleteDatabase(name));
		const snapshot = await second.exportSnapshot();
		check(snapshot.entries.length === 0,
			'another session can load saves after a failed export');

		second.fs.writeFile('/userdata/pending', new Uint8Array([7]));
		const upgraded = await databaseResult(indexedDB.open(name, 2));
		upgraded.close();
		await rejected(() => second.exportSnapshot(),
			'a pending save reports an incompatible database upgrade');
		await databaseResult(indexedDB.deleteDatabase(name));
		await rejected(() => first.exportSnapshot(),
			'the active writer keeps ownership after a failed reopen');
		await second.exportSnapshot();
		second.close();
		const saved = await first.exportSnapshot();
		check(saved.entries.some(entry => entry.path === 'pending' &&
				      entry.data[0] === 7),
			'the active writer can retry and persist its pending save');
	} finally {
		first.close();
		second.close();
	}
	await databaseResult(indexedDB.deleteDatabase(name));
}

async function atomicWrites(storage) {
	const fs = storage.fs;
	check(storage.begin(false) === 0, 'clean commit needs no transaction');
	fs.mkdir('/assets');
	fs.writeFile('/assets/private.ass', new Uint8Array([88]));
	fs.mkdir('/userdata/Profile');
	fs.writeFile('/userdata/Profile/file', new Uint8Array([1, 2, 3]));
	await synchronize(storage);
	fs.writeFile('/userdata/Profile/temporary', new Uint8Array([9, 8]));
	fs.rename('/userdata/Profile/temporary', '/userdata/Profile/file');
	await synchronize(storage);
	let saved = await storage.database.read();
	check(saved.entries.length === 2 &&
			String(saved.entries[1].data) === '9,8',
		'atomic file replacement persists exact bytes and excludes assets');

	const db = storage.database.db;
	const transaction = db.transaction.bind(db);
	db.transaction = (...args) => {
		const pending = transaction(...args);
		if (args[1] === 'readwrite')
			queueMicrotask(() => pending.abort());
		return pending;
	};
	fs.writeFile('/userdata/Profile/file', new Uint8Array([4, 5]));
	storage.begin(false);
	check((await settle(storage)) === -1 && storage.dirty,
		'aborted write retains dirty cache');
	saved = await storage.database.read();
	check(String(saved.entries[1].data) === '9,8',
		'aborted write preserves previous durable data');
	db.transaction = transaction;
	await synchronize(storage);
	saved = await storage.database.read();
	check(String(saved.entries[1].data) === '4,5',
		'retry commits new data');

	// Replacing the same number of bytes at an identical timestamp must persist.
	const time = fs.stat('/userdata/Profile/file').mtime;
	fs.writeFile('/userdata/Profile/file', new Uint8Array([6, 7]));
	fs.utime('/userdata/Profile/file', time, time);
	await synchronize(storage);
	saved = await storage.database.read();
	check(String(saved.entries[1].data) === '6,7',
		'same-size same-timestamp edits persist');

	fs.writeFile('/userdata/during-write', new Uint8Array([1]));
	storage.begin(false);
	fs.writeFile('/userdata/during-write', new Uint8Array([2]));
	await settle(storage);
	check(storage.dirty, 'writes during synchronization remain dirty');
	await synchronize(storage);
	saved = await storage.database.read();
	check(saved.entries.some((entry) => entry.path === 'during-write' &&
			      entry.data[0] === 2),
		'next synchronization persists concurrent edits');
	fs.unlink('/userdata/during-write');
	fs.unlink('/userdata/Profile/file');
	fs.rmdir('/userdata/Profile');
	await synchronize(storage);
	check((await storage.database.read()).entries.length === 0,
		'file and directory deletions persist');
}

async function exclusiveOwnership(storage) {
	const other = await instance();
	other.begin(true);
	check((await settle(other)) === -1,
		'second writer cannot prepare while first owns saves');
	storage.close();
	await new Promise((resolve) => setTimeout(resolve, 20));
	await synchronize(other, true);
	check(other.prepared,
		'second writer can retry after ownership is released');
	other.fs.writeFile('/userdata/cancelled-write', new Uint8Array([17]));
	other.begin(false);
	other.cancel();
	await other.operation;
	await new Promise((resolve) => setTimeout(resolve, 20));
	const next = await instance();
	await synchronize(next, true);
	check(String(next.fs.readFile('/userdata/cancelled-write')) === '17',
		'cancelling notifications lets an accepted transaction finish before releasing ownership');
	next.fs.unlink('/userdata/cancelled-write');
	await synchronize(next);
	return next;
}

// Independently construct malformed but correctly checksummed backup containers.
async function container(table, payload = []) {
	const magic = new TextEncoder().encode('OpenMotor saves\n');
	const metadata = new TextEncoder().encode(JSON.stringify(table));
	const length = new Uint8Array(4);
	new DataView(length.buffer).setUint32(0, metadata.length, true);
	const body =
		new Blob([magic, length, metadata, new Uint8Array(payload)]);
	const hash =
		await crypto.subtle.digest('SHA-256', await body.arrayBuffer());
	return new Blob([body, hash]);
}

async function backups(storage) {
	const original = {
		version: 1,
		entries: [
			{path: 'Profile', directory: true},
			{path: 'Profile/empty', data: new Uint8Array()},
			{
				path: 'Profile/binary',
				data: new Uint8Array([0, 128, 255, 13, 10])
			}
		]
	};
	const backup = await OpenMHBackup.encode(original);
	const copy = await OpenMHBackup.decode(backup);
	check(copy.entries[0].directory && copy.entries[1].data.length === 0 &&
			String(copy.entries[2].data) === '0,128,255,13,10',
		'backup round trip preserves dirs, empty and binary files');
	await rejected(
		() => OpenMHBackup.decode(backup.slice(0, backup.size - 1)),
		'truncated backup rejected');
	const damaged = new Uint8Array(await backup.arrayBuffer());
	damaged[damaged.length - 33] ^= 1;
	await rejected(() => OpenMHBackup.decode(new Blob([damaged])),
		'checksum damage rejected');
	const cases = [
		['unsupported version', {version: 2, entries: []}],
		['null entry', {version: 1, entries: [null]}],
		[
			'path traversal',
			{version: 1, entries: [{path: '../outside', size: 0}]}
		],
		[
			'absolute path',
			{version: 1, entries: [{path: '/outside', size: 0}]}
		],
		[
			'prototype property',
			{version: 1, entries: [{path: '__proto__', size: 0}]}
		],
		[
			'case duplicate',
			{
				version: 1,
				entries: [
					{path: 'A', size: 0},
					{path: 'a', size: 0}
				]
			}
		],
		[
			'missing parent',
			{version: 1, entries: [{path: 'missing/file', size: 0}]}
		],
		[
			'file parent',
			{
				version: 1,
				entries: [
					{path: 'file', size: 0},
					{path: 'file/child', size: 0}
				]
			}
		],
		[
			'negative file size',
			{version: 1, entries: [{path: 'file', size: -1}]}
		],
		[
			'oversized file',
			{version: 1, entries: [{path: 'file', size: 100}]}
		],
		['unconsumed payload', {version: 1, entries: []}, [123]]
	];
	for (const [name, table, payload] of cases) {
		const blob = await container(table, payload);
		await rejected(
			() => OpenMHBackup.decode(blob), name + ' rejected');
	}
	check((await storage.database.read()).entries.length === 0,
		'invalid backups leave durable saves unchanged');
	await storage.database.write(copy);
	storage.close();
	const restored = await instance();
	await synchronize(restored, true);
	check(String(restored.fs.readFile('/userdata/Profile/binary')) ===
			'0,128,255,13,10',
		'imported backup restores through the actual Wasm filesystem');
	restored.close();
}

async function packageCollections() {
	const packages = new OpenMHPackageDatabase();
	const files = [
		new File(['game'], 'game.ass'),
		new File(['patch'], 'patch.ass')
	];
	await packages.clear();
	for (const id of [undefined, null, '', 7]) {
		await rejected(
			() => packages.read(id), 'reading requires a game ID');
		await rejected(() => packages.writeCollection(files, id),
			'writing requires a game ID');
		await rejected(
			() => packages.importCollection(files, () => {}, id),
			'importing requires a game ID');
		await rejected(() => packages.remove(id),
			'removing requires a game ID');
	}
	await packages.writeCollection(files, 'full');
	await packages.importPackage('demo.ass', new Blob(['demo']), 'demo');
	check((await packages.list()).length === 2,
		'separate imports keep both games');
	const record = await packages.read('full');
	check(record.id === 'full' &&
			OpenMHPackageDatabase.entries(record).length === 2,
		'base and patch remain one keyed collection');
	check(await record.files[0].blob.text() === 'game',
		'package bytes survive storage');
	const progress = [];
	await packages.importCollection([new File(['updated'], 'game.ass')],
		(value) => progress.push(value),
		'full',
		record);
	check((await packages.list()).length === 2 &&
			await (await packages.read('full'))
					.files[0]
					.blob.text() === 'updated',
		'replacing a collection preserves the other game');
	check(progress.at(-1).operation === 'complete',
		'import reports durable completion');
	const other = new OpenMHPackageDatabase();
	await rejected(() => other.writeCollection(files, 'full', record),
		'a stale edit cannot overwrite a newer package');
	await rejected(() => other.remove('full', record),
		'a stale removal cannot erase a newer package');
	await rejected(
		() => other.importCollection(files, undefined, 'full', record),
		'a stale preparation cannot replace a newer package');
	const interrupted = {
		name: 'patch.ass',
		size: 1,
		slice() {
			throw new Error('Interrupted read');
		}
	};
	await rejected(() => packages.importCollection(
			       [files[0], interrupted], undefined, 'full'),
		'a failed staging read rejects the import');
	check(await (await packages.read('full')).files[0].blob.text() ===
			'updated',
		'failed staging preserves previously committed bytes');
	Object.defineProperty(
		navigator, 'locks', {value: undefined, configurable: true});
	try {
		let reason = '';
		try {
			await packages.importCollection(
				files, undefined, 'full');
		} catch (error) {
			reason = error.message;
		}
		check(reason.includes('HTTPS or localhost'),
			'missing Web Locks explains the browser requirement');
	} finally {
		delete navigator.locks;
	}
	const ids = ['first', 'second'];
	await Promise.all(
		ids.map(id => new OpenMHPackageDatabase().importPackage(
				'game.ass', new Blob([id]), id)));
	for (const id of ids) {
		const saved = await packages.read(id);
		check(await saved.files[0].blob.text() === id,
			'concurrent imports preserve ' + id + ' package bytes');
	}
	await packages.remove('full');
	check(!(await packages.read('full')) &&
			(await packages.read('demo')).files.length === 1,
		'removing a collection preserves the other game');
	await rejected(() => other.writeCollection(files, 'full', record),
		'a stale edit cannot recreate a removed package');
	for (const older of [{id: 'upload'},
		     {id: 'prepared', importId: 'previous'}]) {
		await packages.transaction('readwrite',
			store => store.put({...older, files: record.files},
				OpenMHPackageDatabase.key(older.id)));
		const original = await other.read(older.id);
		await packages.writeCollection(files, older.id, original);
		check(Boolean((await packages.read(older.id)).revision),
			'an existing ' + older.id +
				' gains a revision on edit');
		await rejected(() => other.remove(older.id, original),
			'an earlier view of the ' + older.id +
				' cannot remove the edit');
	}
	await packages.clear();
}

async function run() {
	if (location.search !== '?reload')
		await failedSaveOpen();
	const storage = await instance();
	await synchronize(storage, true);
	if (location.search === '?reload') {
		checks.push(...JSON.parse(sessionStorage.getItem('checks')));
		check(String(storage.fs.readFile(
			      '/userdata/Profile/binary')) ===
				'0,128,255,13,10',
			'full page reload restores persisted bytes');
		check(!storage.fs.lookupPath('/assets', {
					 noent_okay: true
				 }).node ||
				!storage.fs
					.lookupPath('/assets/private.ass',
						{noent_okay: true})
					.node,
			'asset files are not restored from the save database');
		storage.close();
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify({passed: true, checks})
		});
		return;
	}
	await packageCollections();
	await atomicWrites(storage);
	await backups(await exclusiveOwnership(storage));
	await raceHistory();
	await saveSessions();
	await selectiveReset();
	sessionStorage.setItem('checks', JSON.stringify(checks));
	location.search = '?reload';
}

async function raceHistory() {
	const storage = await instance();
	await synchronize(storage, true);
	const history = new OpenMHRaceHistory(storage.fs);
	const race = {
		date: '2026-11-12T10:00:00.000Z',
		driver: 'Tester',
		track: 'RedRock',
		car: 'Vortex',
		mode: 'Quick Race',
		laps: 1,
		rows: [{
			player: true,
			position: 1,
			finished: true,
			milliseconds: 65000,
			bestLap: 65000,
			estimated: false
		}]
	};
	check(history.read().length === 0, 'fresh saves have no race history');
	for (let i = 0; i < 105; i++)
		history.append({...race, driver: 'Driver ' + i});
	const races = history.read();
	check(races.length === 100 && races[0].driver === 'Driver 104' &&
			races[99].driver === 'Driver 5',
		'history retains the latest 100 races in newest-first order');
	await synchronize(storage);
	const backup = await OpenMHBackup.decode(
		await OpenMHBackup.encode(await storage.database.read()));
	const entry =
		backup.entries.find((item) => item.path === 'racehistory.json');
	check(JSON.parse(new TextDecoder().decode(entry.data))
				.races[0]
				.driver === 'Driver 104',
		'race history survives a save backup round trip');
	storage.fs.writeFile('/userdata/racehistory.json', '{broken');
	await rejected(() => history.append(race),
		'malformed history is not silently replaced');
	check(storage.fs.readFile('/userdata/racehistory.json',
		      {encoding: 'utf8'}) === '{broken',
		'failed append preserves malformed imported history');
	storage.fs.writeFile('/userdata/racehistory.json', entry.data);
	await rejected(
		() => history.append(
			{...race, rows: [{...race.rows[0], milliseconds: -1}]}),
		'invalid result times are rejected');
	await synchronize(storage);
	storage.close();
}

async function saveSessions() {
	const storage = await instance();
	await synchronize(storage, true);
	let requests = 0, closed = false;
	const pauses = [];
	const engine = {
		storage,
		_mh_browser_checkpoint: () => {
			requests++;
			return 1;
		},
		_mh_browser_save_pause: (paused) => {
			pauses.push(paused);
			return 1;
		},
		_mh_browser_close: () => {
			closed = true;
		}
	};
	engine.saves = new OpenMHSaveSession(engine);
	engine.saves.checkpoints.start();
	storage.fs.writeFile('/userdata/backup-current', new Uint8Array([1]));
	await synchronize(storage);
	storage.fs.writeFile('/userdata/backup-current', new Uint8Array([2]));
	const exported =
		await OpenMHBackup.encode(await engine.saves.exportSnapshot());
	const snapshot = await OpenMHBackup.decode(exported);
	check(snapshot.entries.find((e) => e.path === 'backup-current')
				.data[0] === 2,
		'backup API exports dirty current data, not an older database snapshot');
	check(requests === 1 && !storage.dirty && pauses.join(',') === '1,0',
		'export requests a fresh application checkpoint and resumes the host');
	check(engine.saves.checkpoints.active,
		'export restores periodic checkpoints');

	const write = storage.database.write.bind(storage.database);
	storage.database.write = async () => {
		throw new Error('replacement denied');
	};
	await rejected(() => engine.saves.replaceSnapshotAndClose(snapshot),
		'failed import reports the database error');
	check(!closed && !storage.replaced && engine.saves.checkpoints.active &&
			!engine.saves.busy,
		'failed import preserves the running session and restores checkpoint scheduling');
	storage.database.write = write;

	// Hold an existing checkpoint in flight while import acquires save ownership.
	let release;
	const gate = new Promise((resolve) => {
		release = resolve;
	});
	let writes = 0;
	storage.database.write = async (value) => {
		writes++;
		await gate;
		return write(value);
	};
	storage.fs.writeFile('/userdata/backup-current', new Uint8Array([4]));
	storage.begin(false);
	const replacement = {
		version: 1,
		entries: [{path: 'imported', data: new Uint8Array([3])}]
	};
	const importing = engine.saves.replaceSnapshotAndClose(replacement);
	const before = requests;
	engine.saves.checkpoints.request();
	window.dispatchEvent(new PageTransitionEvent('pagehide'));
	await new Promise((resolve) => setTimeout(resolve, 20));
	check(requests === before && writes === 1 &&
			!engine.saves.checkpoints.active,
		'import stops periodic and page-hide checkpoints and waits for the pending write');
	await rejected(() => engine.saves.exportSnapshot(),
		'overlapping backup operations are rejected');
	release();
	await importing;
	check(closed && storage.replaced && !engine.saves.checkpoints.active,
		'successful import retires the old session before allowing reload');
	check(storage.begin(false) === -1,
		'retired cached data cannot commit after replacement');
	storage.database.write = write;
	const saved = await storage.database.read();
	check(saved.entries.length === 1 &&
			saved.entries[0].path === 'imported',
		'the imported snapshot wins over the earlier checkpoint');
	// Restore the fixture's snapshot for the existing reload checks.
	await storage.database.write(snapshot);
	storage.close();
}

async function selectiveReset() {
	const fixtures = {
		settings: ['motorhead.cfg', 'server.npc'],
		controls: ['keyboard.clo', 'wheel.clo'],
		progress: [
			'neocity.flg',
			'league.lgf',
			'race.mde',
			'neocity.cl',
			'neocity03.cr',
			'best.mhi'
		],
		history: ['racehistory.json'],
		unknown: ['notes.txt']
	};
	for (const categories of [['settings'],
		     ['controls'],
		     ['progress'],
		     ['history'],
		     ['settings', 'controls', 'progress', 'history']]) {
		const storage = await instance();
		await synchronize(storage, true);
		const original = storage.snapshot();
		let closed = false;
		const saves = new OpenMHSaveSession({
			storage,
			_mh_browser_checkpoint: () => 1,
			_mh_browser_save_pause: () => 1,
			_mh_browser_close: () => {
				closed = true;
			}
		});
		for (const names of Object.values(fixtures))
			for (const name of names)
				storage.fs.writeFile('/userdata/' + name,
					new Uint8Array([1, 2, 255]));
		await synchronize(storage);
		saves.checkpoints.start();
		await rejected(() => saves.reset(['unknown']),
			'unknown reset category rejects without clearing data');
		await rejected(() => saves.reset([]),
			'empty reset selection rejects without clearing data');
		const write = storage.database.write.bind(storage.database);
		storage.database.write = async () => {
			throw new Error('reset denied');
		};
		await rejected(() => saves.reset(categories),
			'failed reset reports its storage failure');
		check(!closed && !storage.replaced && saves.checkpoints.active,
			'failed reset keeps the session usable and its checkpoints active');
		storage.database.write = write;
		await saves.reset(categories);
		check(closed && storage.replaced && !saves.checkpoints.active,
			'successful reset retires cached settings before reload');
		const snapshot = await storage.database.read();
		for (const [category, names] of Object.entries(fixtures)) {
			for (const name of names) {
				const entry = snapshot.entries.find(
					(entry) => entry.path === name);
				check(categories.includes(category)
						? !entry
						: String(entry?.data) ===
							'1,2,255',
					'reset ' + categories.join('/') +
						' respects ' + name);
			}
		}
		const packages = new OpenMHPackageDatabase();
		await packages.writeCollection(
			[new File(['one'], 'one.ass')], 'one');
		await packages.writeCollection(
			[new File(['two'], 'two.ass')], 'two');
		await packages.clear();
		check(!(await packages.list()).length,
			'package reset removes all game uploads');
		check((await storage.database.read()).entries.length ===
				snapshot.entries.length,
			'package reset preserves saves');
		await storage.database.write(original);
		storage.close();
	}
}

run().catch((error) => fetch('/report', {
	method: 'POST',
	body: JSON.stringify(
		{passed: false, checks, error: error.stack || String(error)})
}));
