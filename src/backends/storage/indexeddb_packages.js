'use strict';

// Asset packages have their own database: forgetting one never touches saves.
class OpenMHPackageDatabase {
	static importLock = 'openmh-package-import';
	static validationWorker = new URL('package-check.js',
		globalThis.document?.currentScript?.src || location.href);

	static async validate(files) {
		const worker = new Worker(this.validationWorker);
		try {
			await new Promise((resolve, reject) => {
				worker.onerror = (event) =>
					reject(new Error(event.message));
				worker.onmessage = ({data}) => data.error
					? reject(new Error(data.error))
					: resolve();
				worker.postMessage(files);
			});
		} finally {
			worker.terminate();
		}
	}

	open() {
		return new Promise((resolve, reject) => {
			const request = indexedDB.open('openmotor-assets', 1);
			let blocked = false;
			request.onupgradeneeded = () =>
				request.result.createObjectStore('packages');
			request.onerror = () => reject(request.error);
			request.onblocked = () => {
				blocked = true;
				reject(new Error(
					'Close other OpenMH tabs and retry.'));
			};
			request.onsuccess = () => {
				if (blocked)
					request.result.close();
				else
					resolve(request.result);
			};
		});
	}

	async transaction(mode, operation) {
		const db = await this.open();
		try {
			return await new Promise((resolve, reject) => {
				const transaction =
					db.transaction('packages', mode);
				const request = operation(
					transaction.objectStore('packages'));
				transaction.oncomplete = () =>
					resolve(request.result);
				transaction.onabort = () => reject(
					transaction.error ||
					new Error(
						'Package storage was interrupted.'));
				transaction.onerror = () => {};
			});
		} finally {
			db.close();
		}
	}

	static key(id) {
		if (typeof id !== 'string' || !id.length)
			throw new Error('A game package ID is required.');
		return 'game/' + id;
	}

	list() {
		return this.transaction('readonly',
			(store) => store.getAll(
				IDBKeyRange.bound('game/', 'game/\uffff')));
	}

	read(id) {
		const key = OpenMHPackageDatabase.key(id);
		return this.transaction('readonly', (store) => store.get(key));
	}

	static entries(record) {
		if (record && !Array.isArray(record.files))
			throw new Error('Unsupported game package record.');
		return record ? record.files : [];
	}

	static revision(record) {
		// Older direct uploads have no token. Their first edit assigns one.
		return record ? record.revision ?? record.importId ?? '' : null;
	}

	static matches(current, expected) {
		if (expected === undefined)
			return true;
		return current?.id === expected?.id &&
			this.revision(current) === this.revision(expected);
	}

	async commitCollection(id, record, expected) {
		const key = OpenMHPackageDatabase.key(id);
		let changed = false;
		await this.transaction('readwrite', (store) => {
			const request = store.get(key);
			request.onsuccess = () => {
				if (!OpenMHPackageDatabase.matches(
					    request.result, expected))
					return;

				if (record)
					store.put(record, key);
				else
					store.delete(key);
				changed = true;
			};
			return request;
		});

		if (!changed)
			throw new Error(
				'This game changed in another tab. Reload the game library and try again.');
	}

	async writeCollection(files, id, expected = null) {
		const entries =
			files.map((file) => ({name: file.name, blob: file}));
		await this.commitCollection(id,
			{id, files: entries, revision: crypto.randomUUID()},
			expected);
		return OpenMHPackageDatabase.key(id);
	}

	clearImport() {
		return this.transaction('readwrite', (store) => {
			const request = store.getAllKeys();
			request.onsuccess = () => {
				for (const key of request.result)
					if (typeof key === 'string' &&
						key.startsWith('prepared/'))
						store.delete(key);
			};
			return request;
		});
	}

	async copyImport(blob, prefix = '', progress = () => {}) {
		const parts = [];
		const chunkSize = 4 * 1024 * 1024;
		for (let offset = 0; offset < blob.size; offset += chunkSize) {
			const bytes =
				await blob.slice(offset, offset + chunkSize)
					.arrayBuffer();
			const key = 'prepared/' + prefix + parts.length;
			await this.transaction('readwrite',
				(store) => store.put(new Blob([bytes]), key));
			parts.push(await this.transaction(
				'readonly', (store) => store.get(key)));
			progress(Math.min(blob.size, offset + chunkSize));
		}
		return new Blob(parts);
	}

	// File clones can retain an OPFS backing file. Copy bounded chunks and
	// read disk-owned Blobs back before the source workspace is removed.
	async importPackage(name, blob, id, expected = null) {
		return this.importCollection(
			[new File([blob], name)], () => {}, id, expected);
	}

	async importCollection(
		files, progress = () => {}, id, expected = null) {
		if (!navigator.locks)
			throw new Error(
				'Saving prepared packages requires a browser with Web Locks on HTTPS or localhost.');
		return navigator.locks.request(OpenMHPackageDatabase.importLock,
			() => this.importLocked(files, progress, id, expected));
	}

	async importLocked(files, progress, id, expected) {
		OpenMHPackageDatabase.key(id);
		const total = files.reduce((sum, file) => sum + file.size, 0);
		let completed = 0;
		const report = (bytes) => progress({
			stage: 'save',
			operation: 'save',
			completed: completed + bytes,
			total
		});

		report(0);
		const revision = crypto.randomUUID();
		await this.clearImport();
		try {
			const entries = [];
			for (const [index, file] of files.entries()) {
				entries.push({
					name: file.name,
					blob: await this.copyImport(
						file, index + '/', report)
				});
				completed += file.size;
			}

			progress({
				stage: 'save',
				operation: 'commit',
				completed: 0,
				total: 0
			});
			await this.commitCollection(
				id, {id, files: entries, revision}, expected);
		} finally {
			try {
				await this.clearImport();
			} catch (error) {
				// The next import removes stale chunks before it starts.
				// Do not hide the result of the atomic package replacement.
				console.warn(
					'Temporary package chunks could not be removed.',
					error);
			}
		}

		// Firefox can invalidate earlier Blob references when their staging
		// records are deleted. Reopen the committed record after cleanup.
		const record = await this.read(id);
		if (record?.revision !== revision)
			throw new Error(
				'Another tab replaced this package. Reload to use the current package.');
		progress({
			stage: 'save',
			operation: 'complete',
			completed: total,
			total
		});
		return record;
	}

	remove(id, expected) {
		return this.commitCollection(id, null, expected);
	}

	clear() {
		// Only remembered uploads live here; hosted built-in assets are separate.
		const clear = () =>
			this.transaction('readwrite', (store) => store.clear());
		return navigator.locks
			? navigator.locks.request(
				  OpenMHPackageDatabase.importLock, clear)
			: clear();
	}
}
