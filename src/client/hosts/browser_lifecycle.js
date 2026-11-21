'use strict';

class OpenMHLifecycle {
	constructor(canvas, suspend, report) {
		this.canvas = canvas;
		this.suspend = suspend;
		this.report = report;
		this.active = this.paused = this.failed = false;
		this.contextLost = this.pageHidden = false;
		this.onHidden = null;
		this.runHidden = null;
		this.visibility = () => this.visibilityChanged();
		this.hide = () => {
			this.pageHidden = true;
			this.visibilityChanged();
		};
		this.show = () => {
			this.pageHidden = false;
			this.update();
		};
		this.lost = (event) => {
			event.preventDefault();
			this.contextLost = true;
			this.update();
		};
		this.restored = () => {
			this.contextLost = false;
			// Let the backend's restoration listener publish its generation first.
			queueMicrotask(() => this.update());
		};
	}

	start() {
		this.active = true;
		document.addEventListener('visibilitychange', this.visibility);
		window.addEventListener('pagehide', this.hide);
		window.addEventListener('pageshow', this.show);
		this.canvas.addEventListener('webglcontextlost', this.lost);
		this.canvas.addEventListener(
			'webglcontextrestored', this.restored);
		this.update();
	}

	stop() {
		this.active = false;
		document.removeEventListener(
			'visibilitychange', this.visibility);
		window.removeEventListener('pagehide', this.hide);
		window.removeEventListener('pageshow', this.show);
		this.canvas.removeEventListener('webglcontextlost', this.lost);
		this.canvas.removeEventListener(
			'webglcontextrestored', this.restored);
	}

	setPaused(paused) {
		this.userPaused = !!paused;
		this.update();
	}

	visibilityChanged() {
		if (this.active && (document.hidden || this.pageHidden))
			this.onHidden?.();
		this.update();
	}

	update() {
		if (!this.active)
			return;
		const paused = !!this.userPaused ||
			(document.hidden && !this.runHidden?.()) ||
			this.pageHidden || this.contextLost;
		if (paused === this.paused && !this.failed)
			return;
		this.failed = !this.suspend(paused ? 1 : 0);
		if (!this.failed)
			this.paused = paused;
		this.report({
			paused: this.paused,
			failed: this.failed,
			reason: this.contextLost  ? 'graphics'
				: this.userPaused ? 'user'
						  : 'hidden'
		});
	}
}
