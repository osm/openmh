'use strict';

// Own the temporary browser hooks and output observer for one queue test.
class AudioQueueProbe {
	constructor() {
		this.checks = [];
		this.errors = [];
		this.outputs = [];
		this.sourceStarts = this.delayedCallbacks = this.audioFrame = 0;
		this.delayIntervals = false;
		this.connect = AudioNode.prototype.connect;
		this.startSource = AudioBufferSourceNode.prototype.start;
		this.interval = window.setInterval;
	}

	installHooks() {
		const probe = this;
		AudioNode.prototype.connect = function(destination, ...args) {
			if (destination instanceof AudioDestinationNode)
				probe.outputs.push(this);
			return probe.connect.call(this, destination, ...args);
		};
		AudioBufferSourceNode.prototype.start = function(...args) {
			probe.sourceStarts++;
			return probe.startSource.apply(this, args);
		};
		window.setInterval = (callback, delay, ...args) =>
			this.interval.call(window, () => {
				if (this.delayIntervals)
					this.delayedCallbacks++;
				else
					callback(...args);
			}, delay);
	}

	async open() {
		this.installHooks();
		this.game = await createAudioQueueCheck({
			print: (text) => this.checks.push(text),
			printErr: (text) => this.errors.push(text)
		});
		this.step('open_streams');
		if (this.outputs.length !== 1)
			throw new Error(
				'Expected one shared browser audio context');
		const output = this.outputs[0];
		this.context = output.context;
		await this.context.audioWorklet.addModule(
			'/audio_queue_worklet.js');
		this.tap = new AudioWorkletNode(this.context, 'continuity');
		output.disconnect();
		output.connect(this.tap);
		this.connect.call(this.tap, this.context.destination);
		this.step('start_streams');
	}

	step(scenario) {
		if (this.game.ccall('mh_browser_audio_queue',
			    'number',
			    ['string'],
			    [scenario]))
			throw new Error('Queue scenario ' + scenario + ': ' +
				this.errors.join('\n'));
	}

	message(command) {
		return new Promise((resolve) => {
			this.tap.port.onmessage = (event) =>
				resolve(event.data);
			this.tap.port.postMessage(command);
		});
	}

	startFramePump() {
		const pump = () => {
			this.game._mh_browser_audio_update();
			this.audioFrame = requestAnimationFrame(pump);
		};
		pump();
	}

	stopFramePump() {
		cancelAnimationFrame(this.audioFrame);
		this.audioFrame = 0;
	}

	close() {
		this.stopFramePump();
		this.delayIntervals = false;
		window.setInterval = this.interval;
		try {
			if (this.game)
				this.step('close_streams');
		} finally {
			AudioNode.prototype.connect = this.connect;
			AudioBufferSourceNode.prototype.start =
				this.startSource;
			this.tap?.disconnect();
		}
	}
}
