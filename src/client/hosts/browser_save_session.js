'use strict';

// Coordinate application checkpoints and storage snapshots on the host thread.
class OpenMHSaveSession {
	constructor(engine) {
		this.engine = engine;
		this.checkpoints = new OpenMHCheckpoints(
			() => engine._mh_browser_checkpoint());
		this.busy = false;
	}

	stop() {
		this.closed = true;
		this.checkpoints.stop();
	}

	async exclusive(operation) {
		if (this.busy)
			throw new Error('A save operation is already running.');
		this.busy = true;
		const active = this.checkpoints.active;
		let failure;
		this.checkpoints.stop();
		try {
			if (!this.engine._mh_browser_save_pause(1))
				throw new Error(
					'Cannot pause the game for a save operation.');
			return await operation();
		} catch (error) {
			failure = error;
			throw error;
		} finally {
			this.busy = false;
			const resumed = this.engine._mh_browser_save_pause(0);
			if (active && !this.closed &&
				!this.engine.storage.replaced)
				this.checkpoints.start();
			if (!resumed && !failure)
				throw new Error(
					'Cannot resume the game after saving.');
		}
	}

	async checkpoint() {
		// Flush application settings even while animation frames are suspended.
		while (true) {
			const result = this.engine._mh_browser_checkpoint();
			if (result < 0)
				throw new Error(this.engine.storage.error ||
					'Saves need an explicit retry.');
			await this.engine.storage.settle();
			if (result === 1)
				return;
			await new Promise((resolve) => setTimeout(resolve, 0));
		}
	}

	async flush() {
		return this.exclusive(() => this.checkpoint());
	}

	async exportSnapshot() {
		return this.exclusive(async () => {
			await this.checkpoint();
			return this.engine.storage.exportSnapshot();
		});
	}

	async replaceSnapshotAndClose(snapshot) {
		return this.exclusive(() => this.replaceAndClose(snapshot));
	}

	async replaceAndClose(snapshot) {
		await this.engine.storage.replaceSnapshot(snapshot);
		// Close only after durable replacement; stale settings must not return.
		this.engine._mh_browser_close();
		await this.engine.storage.settle(true);
		this.engine.storage.close();
	}

	static category(entry) {
		if (entry.directory)
			return null;
		const name = entry.path.split('/').at(-1).toLowerCase();
		if (name === 'motorhead.cfg' || name.endsWith('.npc'))
			return 'settings';
		if (name.endsWith('.clo'))
			return 'controls';
		if (name === 'racehistory.json')
			return 'history';
		if (/\.(flg|lgf|mde|cl|cr|mhi)$/.test(name))
			return 'progress';
		return null;
	}

	async reset(categories) {
		if (!Array.isArray(categories) || !categories.length ||
			categories.some((category) => !['settings',
						'controls',
						'progress',
						'history']
						.includes(category)))
			throw new Error('Choose valid reset categories.');
		const selected = new Set(categories);
		return this.exclusive(async () => {
			await this.checkpoint();
			const snapshot =
				await this.engine.storage.exportSnapshot();
			const entries = snapshot.entries.filter(
				(entry) => !selected.has(
					OpenMHSaveSession.category(entry)));
			await this.replaceAndClose({...snapshot, entries});
		});
	}
}
