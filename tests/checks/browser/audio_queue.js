'use strict';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function stall(ms) {
	const started = performance.now(), end = started + ms;
	while (performance.now() < end) {
		/* Deliberately occupy the rendering thread. */
	}
	return performance.now() - started;
}

function checkContinuity(result, sampleRate, scenario) {
	if (result.frames < sampleRate * 2)
		throw new Error(scenario + ': insufficient audio captured: ' +
			JSON.stringify(result));
	if (result.silent || result.repeated || result.sequenceErrors)
		throw new Error(scenario +
			': audio interrupted: ' + JSON.stringify(result));
}

async function checkResponseDelay(probe) {
	await probe.message('watch');
	const changed = new Promise((resolve) => {
		probe.tap.port.onmessage = (event) =>
			resolve(event.data.changedAt);
	});
	const requestedAt = probe.context.currentTime;
	probe.game._mh_browser_audio_level();
	const responseMs = ((await changed) - requestedAt) * 1000;
	if (!(responseMs >= 0 && responseMs < 100))
		throw new Error('Queued output delays a sound change by ' +
			responseMs + ' ms');
	probe.checks.push(
		'a changed sound reaches browser output within 100 ms');
	return responseMs;
}

async function checkShortStalls(probe) {
	probe.sourceStarts = 0;
	const startedAt = probe.context.currentTime;
	await probe.message('start');
	let longestStallMs = 0;
	for (let i = 0; i < 40; i++) {
		await sleep(80);
		longestStallMs = Math.max(longestStallMs, stall(60));
	}
	const continuity = await probe.message('stop');
	const sourcesPerSecond =
		probe.sourceStarts / (probe.context.currentTime - startedAt);
	continuity.sampleRate = probe.context.sampleRate;
	continuity.longestStallMs = longestStallMs;
	checkContinuity(continuity,
		probe.context.sampleRate,
		'short main-thread stalls');
	probe.checks.push(
		'actual browser output has no silence or repeated blocks during 60 ms main-thread stalls');
	probe.checks.push(
		'mixer writes and service boundaries preserve every output sample in order');
	if (sourcesPerSecond > 130)
		throw new Error('Excessive browser source creation: ' +
			sourcesPerSecond + '/s for two streams');
	probe.checks.push(
		'small mixer writes are batched before creating browser audio sources');
	return {continuity, sourcesPerSecond};
}

async function checkDelayedTimers(probe) {
	// RAF can continue while ordinary timer tasks, including both OpenAL timers, wait.
	probe.startFramePump();
	probe.delayIntervals = true;
	await probe.message('start');
	await sleep(2500);
	probe.delayIntervals = false;
	probe.stopFramePump();
	const result = await probe.message('stop');
	if (!probe.delayedCallbacks)
		throw new Error('No interval callbacks were delayed');
	checkContinuity(
		result, probe.context.sampleRate, 'delayed interval callbacks');
	probe.checks.push(
		'frame-driven refills preserve audio when browser interval callbacks are delayed');
	return result;
}

async function checkSuspensionAndRecovery(probe) {
	probe.step('suspend_streams');
	await sleep(180);
	probe.step('resume_streams');
	await sleep(250);
	stall(250);
	await sleep(200);
	probe.step('check_recovery_and_clear');
}

async function checkFiniteStream(probe) {
	probe.step('open_finite_stream');
	await sleep(350);
	probe.step('check_finite_drain');
}

async function run() {
	const probe = new AudioQueueProbe();
	try {
		await probe.open();
		await sleep(350);
		const responseMs = await checkResponseDelay(probe);
		const {continuity, sourcesPerSecond} =
			await checkShortStalls(probe);
		const frameContinuity = await checkDelayedTimers(probe);
		await checkSuspensionAndRecovery(probe);
		await checkFiniteStream(probe);
		return {
			passed: true,
			checks: probe.checks,
			continuity,
			frameContinuity,
			responseMs,
			sourcesPerSecond
		};
	} finally {
		probe.close();
	}
}

run().catch((error) => ({passed: false, error: error.stack || String(error)}))
	.then((report) => fetch('/report',
		      {method: 'POST', body: JSON.stringify(report)}));
