'use strict';

/* The mount is private to one Wasm instance. Packaged assets never enter it. */
class OpenMHStorage {
	constructor(game) {
		this.fs = game.FS;
		this.database = new OpenMHSaveDatabase();
		this.root = '/userdata';
		this.status = 0;
		this.dirty = false;
		this.restoring = false;
		this.error = '';
		this.generation = 0;
		this.revision = 0;
		this.tracked = new WeakSet();
		this.fs.mkdir(this.root);
		const node = this.fs.mount(game.MEMFS, {}, this.root);
		this.track(node);
	}

	track(node) {
		if (this.tracked.has(node))
			return;

		this.tracked.add(node);

		for (const [table, names] of [[
			     'node_ops',
			     [
				     'mknod',
				     'rename',
				     'unlink',
				     'rmdir',
				     'symlink',
				     'setattr'
			     ]
		     ],
			     ['stream_ops', ['write', 'msync', 'allocate']]]) {
			node[table] = {...node[table]};

			for (const name of names)
				this.trackOperation(node[table], name);
		}
	}

	trackOperation(operations, name) {
		const original = operations[name];
		const storage = this;

		if (!original)
			return;

		operations[name] = function(...args) {
			const result = original.apply(this, args);

			if (name === 'mknod' || name === 'symlink')
				storage.track(result);

			if (!storage.restoring) {
				storage.dirty = true;
				storage.revision++;
			}

			return result;
		};
	}

	snapshot() {
		const entries = [];

		this.collectEntries('', entries);

		return {version: 1, entries};
	}

	collectEntries(relative, entries) {
		const path = this.root + (relative ? '/' + relative : '');

		for (const name of this.fs.readdir(path).sort()) {
			if (name === '.' || name === '..')
				continue;

			const child = relative ? relative + '/' + name : name;
			const stat = this.fs.lstat(this.root + '/' + child);

			if (this.fs.isDir(stat.mode)) {
				entries.push({path: child, directory: true});
				this.collectEntries(child, entries);
			} else if (this.fs.isFile(stat.mode)) {
				const data = this.fs.readFile(
					this.root + '/' + child);

				entries.push({path: child, data});
			} else {
				throw new Error(
					'Unsupported entry in browser saves: ' +
					child);
			}
		}
	}

	static validPathPart(part) {
		return (part && part !== '.' && part !== '..' &&
			!/[\\\0]/.test(part) &&
			!Object.prototype.hasOwnProperty.call(
				Object.prototype, part));
	}

	static validateEntry(entry) {
		if (!entry || typeof entry.path !== 'string')
			throw new Error(
				'Invalid browser save entry: missing path.');

		const parts = entry.path.split('/');

		const label = JSON.stringify(entry.path);
		if (!entry.path || entry.path.length > 512 ||
			parts.length > 64 ||
			!parts.every((part) => this.validPathPart(part)))
			throw new Error(
				'Invalid browser save path: ' + label + '.');
		if (entry.directory !== undefined && entry.directory !== true)
			throw new Error(
				'Invalid browser save directory flag: ' +
				label + '.');
		if (entry.directory !== true &&
			!(entry.data instanceof Uint8Array))
			throw new Error('Invalid browser save file data: ' +
				label + '.');
	}

	static validate(snapshot) {
		if (!snapshot || snapshot.version !== 1 ||
			!Array.isArray(snapshot.entries))
			throw new Error('Unsupported browser save data.');

		const paths = new Map();
		const directories = new Set();

		for (const entry of snapshot.entries) {
			this.validateEntry(entry);

			const folded = entry.path.replace(
				/[A-Z]/g, (letter) => letter.toLowerCase());
			const parent = entry.path.slice(
				0, entry.path.lastIndexOf('/'));

			if (paths.has(folded))
				throw new Error(
					'Conflicting browser save paths: ' +
					JSON.stringify(paths.get(folded)) +
					' and ' + JSON.stringify(entry.path) +
					'.');

			if (entry.path.includes('/') &&
				!directories.has(parent))
				throw new Error(
					'Missing browser save directory.');

			paths.set(folded, entry.path);

			if (entry.directory === true)
				directories.add(entry.path);
		}
	}

	restore(snapshot) {
		OpenMHStorage.validate(snapshot);
		this.restoring = true;
		try {
			for (const entry of snapshot.entries) {
				const path = this.root + '/' + entry.path;
				if (entry.directory)
					this.fs.mkdirTree(path);
				else
					this.fs.writeFile(path, entry.data);
			}
		} finally {
			this.restoring = false;
		}
	}

	async prepare() {
		if (this.prepared)
			return;
		const snapshot = await this.database.read();
		if (snapshot !== undefined)
			this.restore(snapshot);
		this.dirty = false;
		this.prepared = true;
	}

	async commit() {
		const revision = this.revision;
		const snapshot = this.snapshot();
		await this.database.write(snapshot);
		if (this.revision === revision)
			this.dirty = false;
		return snapshot;
	}

	// The host quiesces application updates and checkpoint scheduling first.
	async settle(allowFailure = false) {
		await this.operation;
		if (!allowFailure && this.status < 0)
			throw new Error(this.error ||
				'Browser saves need an explicit retry.');
	}

	async exportSnapshot() {
		if (this.replaced)
			throw new Error('Reload to use the imported saves.');
		await this.settle();
		await this.prepare();
		return this.dirty ? this.commit() : this.snapshot();
	}

	async replaceSnapshot(snapshot) {
		OpenMHStorage.validate(snapshot);
		await this.settle(true);
		// A failed replacement preserves the current cache and database.
		await this.database.write(snapshot);
		// The old cache must never overwrite the replacement, even during teardown.
		this.replaced = true;
	}

	begin(prepare) {
		if (this.replaced) {
			this.error = 'Reload to use the imported saves.';
			this.status = -1;
			return -1;
		}
		if (this.status === 1)
			return 1;
		if (prepare && this.prepared)
			return 0;
		if (!prepare && !this.dirty)
			return 0;
		const generation = ++this.generation;
		this.status = 1;
		this.error = '';
		this.operation =
			(prepare ? this.prepare() : this.commit())
				.then(
					() => {
						if (generation ===
							this.generation)
							this.status = 0;
					},
					(error) => {
						if (generation ===
							this.generation) {
							this.error =
								error?.message ||
								String(error);
							this.status = -1;
						}
					});
		return 1;
	}

	cancel() {
		// Detach completion notifications; no callback holds a C allocation.
		this.generation++;
		this.operation?.finally(() => {
			this.status = 0;
			this.database.close();
		});
	}

	close() {
		if (this.status === 1)
			throw new Error('Save operation is still pending.');
		this.database.close();
	}
}
