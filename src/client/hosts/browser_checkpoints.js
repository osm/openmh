'use strict';

// Save during normal use, rather than relying on a final browser exit event.
class OpenMHCheckpoints {
	constructor(checkpoint) {
		this.checkpoint = checkpoint;
		this.active = this.failed = false;
		this.visibility = () => {
			if (document.hidden)
				this.request();
		};
		this.hide = () => this.request();
	}

	start() {
		this.active = true;
		this.periodic = setInterval(() => this.request(), 10000);
		document.addEventListener('visibilitychange', this.visibility);
		window.addEventListener('pagehide', this.hide);
	}

	stop() {
		this.active = false;
		clearInterval(this.periodic);
		clearTimeout(this.pending);
		document.removeEventListener(
			'visibilitychange', this.visibility);
		window.removeEventListener('pagehide', this.hide);
	}

	request() {
		if (!this.active || this.failed)
			return;
		clearTimeout(this.pending);
		const result = this.checkpoint();
		this.failed = result < 0;
		// A prior commit may be in flight. Coalesce requests until it finishes.
		if (this.active && result === 0)
			this.pending = setTimeout(() => this.request(), 100);
	}

	retry() {
		if (!this.failed)
			return;
		this.failed = false;
		this.request();
	}
}
