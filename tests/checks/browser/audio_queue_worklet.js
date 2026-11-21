'use strict';

class Continuity extends AudioWorkletProcessor {
	constructor() {
		super();
		this.active = false;
		this.port.onmessage = (event) => {
			if (event.data === 'watch') {
				this.watching = true;
				this.port.postMessage({watching: true});
			} else if (event.data === 'start') {
				this.frames = this.silent = this.repeated =
					this.run = this.longest = 0;
				this.sequenceErrors = 0;
				this.lastSample = null;
				this.previous = null;
				this.positions = [];
				this.active = true;
				this.port.postMessage({started: true});
			} else {
				this.active = false;
				this.port.postMessage({
					frames: this.frames,
					silent: this.silent,
					repeated: this.repeated,
					longest: this.longest,
					positions: this.positions,
					sequenceErrors: this.sequenceErrors
				});
			}
		};
	}

	process(inputs, outputs) {
		const samples = inputs[0]?.[0];
		if (this.watching && samples) {
			const changed =
				samples.findIndex((value) => value > 0.3);
			if (changed !== -1) {
				this.watching = false;
				this.port.postMessage({
					changedAt: currentTime +
						changed / sampleRate
				});
			}
		}
		for (let c = 0; c < outputs[0].length; c++)
			if (inputs[0]?.[c])
				outputs[0][c].set(inputs[0][c]);
		if (this.active) {
			const block = samples || new Float32Array(128);
			let repeated = !!this.previous;
			for (let i = 0; i < block.length; i++) {
				const sample =
					Math.round((block[i] - 0.375) * 8192);
				if (this.lastSample !== null &&
					sample !== (this.lastSample + 1) % 1009)
					this.sequenceErrors++;
				this.lastSample = sample;
				if (Math.abs(block[i]) < 0.001) {
					this.silent++;
					this.run++;
					if (this.positions.length < 10)
						this.positions.push(
							this.frames + i);
					this.longest = Math.max(
						this.longest, this.run);
				} else
					this.run = 0;
				if (this.previous?.[i] !== block[i])
					repeated = false;
			}
			if (repeated)
				this.repeated++;
			this.previous = block.slice();
			this.frames += block.length;
		}
		return true;
	}
}

registerProcessor('continuity', Continuity);
