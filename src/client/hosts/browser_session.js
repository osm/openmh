'use strict';

// Browser application ownership shared by custom pages and original-menu hosts.
// Pages use this facade; filesystem paths and C exports stay inside the runtime.
class OpenMHSession {
	static async create({canvas, onSuspension = () => {}, ...callbacks}) {
		const engine = await createOpenMH({canvas, ...callbacks});
		engine.storage = new OpenMHStorage(engine);
		engine.saves = new OpenMHSaveSession(engine);
		return new OpenMHSession(engine, canvas, onSuspension);
	}

	constructor(engine, canvas, onSuspension) {
		this.engine = engine;
		this.canvas = canvas;
		this.lifecycle = new OpenMHLifecycle(canvas,
			(value) => engine._mh_browser_set_suspended(value),
			onSuspension);
		const exited = engine.onGameExit;
		engine.onGameExit = (result) => {
			this.running = false;
			if (!this.player)
				this.stop();
			exited?.(result);
		};
	}

	async mount(files) {
		if (this.mounted || this.mounting)
			throw new Error('This session already has assets.');
		if (!files.length)
			throw new Error('Choose at least one asset package.');
		const fs = this.engine.FS;
		const directory = '/assets';
		fs.mkdir(directory);
		this.mounting = true;
		try {
			for (const [index, file] of files.entries()) {
				fs.writeFile(
					directory + '/package' + index + '.ass',
					new Uint8Array(
						await file.arrayBuffer()));
			}
			this.mounted = true;
		} catch (error) {
			for (const name of fs.readdir(directory))
				if (name !== '.' && name !== '..')
					fs.unlink(directory + '/' + name);
			fs.rmdir(directory);
			throw error;
		} finally {
			this.mounting = false;
		}
	}

	async start({frontend = 'web', arguments: args = []} = {}) {
		if (!this.mounted || this.started)
			throw new Error(
				'Mount assets before starting a new session.');
		if (!['web', 'original'].includes(frontend))
			throw new Error('Unknown frontend.');
		this.started = this.running = true;
		try {
			if (frontend === 'web') {
				this.player = await OpenMH.attach(this.engine, {
					canvas: this.canvas,
					lifecycle: this.lifecycle,
					arguments: args
				});
				return this.player;
			}
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
			if (this.running) {
				this.lifecycle.start();
				this.engine.saves.checkpoints.start();
			}
			return null;
		} catch (error) {
			this.stop();
			throw error;
		}
	}

	stop() {
		this.lifecycle.stop();
		this.engine.saves.stop();
		if (this.engine.storage.status === 1)
			this.engine.storage.cancel();
		else
			this.engine.storage.close();
	}

	resume() {
		this.lifecycle.update();
	}

	retrySaves() {
		if (!this.engine._mh_browser_retry_storage())
			return false;
		this.engine.saves.checkpoints.retry();
		return true;
	}

	exportSnapshot() {
		return this.engine.saves.exportSnapshot();
	}

	replaceSnapshotAndClose(snapshot) {
		return this.engine.saves.replaceSnapshotAndClose(snapshot);
	}

	resetData(categories) {
		if (this.player &&
			(this.player.commandBusy ||
				this.player.multiplayer.engaged ||
				!['ready', 'finished', 'failed'].includes(
					this.player.state)))
			throw new Error(
				'Leave the race or server before resetting data.');
		return this.engine.saves.reset(categories);
	}
}
