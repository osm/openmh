'use strict';

class OpenMHSaveDatabase {
	async open() {
		if (this.db)
			return this.db;

		const acquired = await this.acquireLock();
		try {
			this.db = await this.openDatabase();
		} catch (error) {
			// An existing session still owns its cached saves.
			if (acquired)
				this.close();
			throw error;
		}
		this.db.onversionchange = () => {
			this.db.close();
			this.db = null;
		};

		return this.db;
	}

	async acquireLock() {
		if (this.releaseLock)
			return false;

		if (!navigator.locks)
			throw new Error(
				'Browser saves require HTTPS or localhost.');

		await new Promise((resolve, reject) => {
			const hold = (lock) =>
				this.holdLock(lock, resolve, reject);

			navigator.locks
				.request('openmotor-user-data',
					{ifAvailable: true},
					hold)
				.catch(reject);
		});
		return true;
	}

	holdLock(lock, acquired, rejected) {
		if (!lock) {
			rejected(new Error(
				'Close the other OpenMH tab before loading saves.'));
			return;
		}

		// Resolving acquisition starts use; resolving this promise releases ownership.
		return new Promise((release) => {
			this.releaseLock = release;
			acquired();
		});
	}

	openDatabase() {
		return new Promise((resolve, reject) => {
			const request =
				indexedDB.open('openmotor-user-data', 1);
			let blocked = false;
			request.onupgradeneeded = () =>
				request.result.createObjectStore('saves');
			request.onsuccess = () => {
				if (blocked)
					request.result.close();
				else
					resolve(request.result);
			};
			request.onerror = () => reject(request.error);
			request.onblocked = () => {
				blocked = true;
				reject(new Error(
					'Close other OpenMH tabs and retry.'));
			};
		});
	}

	async read() {
		const db = await this.open();
		return new Promise((resolve, reject) => {
			const transaction = db.transaction('saves', 'readonly');
			const request =
				transaction.objectStore('saves').get('current');
			transaction.oncomplete = () => resolve(request.result);
			transaction.onabort = () => reject(transaction.error ||
				new Error('Reading saves was interrupted.'));
			transaction.onerror = () => {};
		});
	}

	async write(snapshot) {
		const db = await this.open();
		return new Promise((resolve, reject) => {
			const transaction = db.transaction(
				'saves', 'readwrite', {durability: 'strict'});
			transaction.objectStore('saves').put(
				snapshot, 'current');
			transaction.oncomplete = resolve;
			transaction.onabort = () => reject(transaction.error ||
				new Error(
					'Saving was interrupted. Retry to keep your changes.'));
			transaction.onerror = () => {};
		});
	}

	close() {
		this.db?.close();
		this.db = null;
		this.releaseLock?.();
		this.releaseLock = null;
	}
}
