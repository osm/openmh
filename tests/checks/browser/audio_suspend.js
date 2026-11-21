'use strict';

async function run() {
	const checks = [], errors = [];
	const createProcessor = AudioContext.prototype.createScriptProcessor;
	let processors = 0, callbacks = 0;
	AudioContext.prototype.createScriptProcessor = function(...args) {
		processors++;
		const node = createProcessor.apply(this, args);
		node.addEventListener('audioprocess', () => callbacks++);
		return node;
	};
	const game = await createAudioSuspendCheck({
		print: (text) => checks.push(text),
		printErr: (text) => errors.push(text)
	});
	const step = (scenario) => {
		if (game.ccall('mh_browser_audio_suspend',
			    'number',
			    ['string'],
			    [scenario]))
			throw new Error('Audio scenario ' + scenario + ': ' +
				errors.join('\n'));
	};
	try {
		for (const scenario of ['open_streams',
			     'suspend_streams',
			     'check_suspended_streams',
			     'resume_streams',
			     'check_resumed_streams',
			     'check_remaining_streams',
			     'reopen_stream',
			     'check_reopened_stream']) {
			step(scenario);
			if (scenario === 'open_streams') {
				// Backend startup can outlast a fixed delay on a busy host.
				const deadline = performance.now() + 5000;
				while (callbacks < 2) {
					if (performance.now() > deadline)
						throw new Error(
							'Browser audio output did not start');
					await new Promise(
						(resolve) => setTimeout(
							resolve, 25));
				}
			}
			await new Promise(
				(resolve) => setTimeout(resolve, 350));
		}
		if (processors !== 1)
			throw new Error(
				'Only the independent SDL provider may create a ScriptProcessor');
		checks.push(
			'queued browser output needs no main-thread ScriptProcessor');
	} finally {
		step('close_streams');
		AudioContext.prototype.createScriptProcessor = createProcessor;
	}
	return {passed: true, checks};
}

run().catch((error) => ({passed: false, error: error.stack || String(error)}))
	.then((report) => fetch('/report',
		      {method: 'POST', body: JSON.stringify(report)}));
