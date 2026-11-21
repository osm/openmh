'use strict';

// Owns one disposable worker and its private disk workspace. The lock spans
// publication, so another tab cannot delete files still being remembered.
class OpenMHExtractionJob {
	constructor(workerURL) {
		this.workerURL = workerURL;
	}

	static supported() {
		return (isSecureContext && typeof Worker === 'function' &&
			navigator.storage?.getDirectory && navigator.locks);
	}

	async cleanup() {
		const root = await navigator.storage.getDirectory();
		for (let attempt = 0;; attempt++) {
			try {
				await root.removeEntry(
					'openmh-extractor', {recursive: true});
				return;
			} catch (error) {
				if (error.name === 'NotFoundError')
					return;
				// Termination releases synchronous handles asynchronously.
				if (attempt === 49)
					throw error;
				await new Promise(
					(resolve) => setTimeout(resolve, 100));
			}
		}
	}

	extract(request, signal, progress) {
		return new Promise((resolve, reject) => {
			const worker = new Worker(this.workerURL);
			this.worker = worker;
			const abort = () => reject(new DOMException(
				'Preparation cancelled.', 'AbortError'));
			signal.addEventListener('abort', abort, {once: true});
			this.removeAbort = () =>
				signal.removeEventListener('abort', abort);
			worker.onerror = (event) =>
				reject(new Error(event.message ||
					'The extractor could not start.'));
			worker.onmessage = ({data}) => {
				if (data.type === 'progress')
					progress(data.progress);
				else if (data.type === 'complete')
					resolve(data.file);
				else if (data.type === 'error')
					reject(new Error(data.message));
			};
			if (signal.aborted)
				abort();
			else
				worker.postMessage(request);
		});
	}

	stop() {
		this.removeAbort?.();
		this.worker?.terminate();
		this.worker = null;
	}

	async runLocked(request, signal, progress, publish) {
		await this.cleanup();
		try {
			const file =
				await this.extract(request, signal, progress);
			if (signal.aborted)
				throw new DOMException(
					'Preparation cancelled.', 'AbortError');
			// Once publication starts, cancellation must not report failure
			// after a successful commit.
			this.removeAbort();
			return await publish(file);
		} finally {
			this.stop();
			try {
				await this.cleanup();
			} catch (error) {
				// Preserve the preparation/publication result. The next job
				// must remove this stale workspace before it can start.
				console.warn(
					'Temporary extraction storage could not be removed.',
					error);
			}
		}
	}

	async run(request, signal, progress, publish) {
		if (!OpenMHExtractionJob.supported())
			throw new Error(
				'Local preparation needs browser disk storage on HTTPS or localhost. You can still load a .ass made with native openmotorex.');

		return navigator.locks.request(
			'openmh-extractor', {ifAvailable: true}, (lock) => {
				if (!lock)
					throw new Error(
						'Another tab is preparing a game. Wait for it to finish.');
				return this.runLocked(
					request, signal, progress, publish);
			});
	}
}
