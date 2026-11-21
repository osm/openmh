'use strict';

async function run() {
	const canvas = document.getElementById('canvas');
	const calls = [], reports = [], checks = [];
	let rejectResume = false, generation = 0, resumedGeneration = 0;
	const owner = new OpenMHLifecycle(canvas, (paused) => {
		calls.push(paused);
		if (!paused && rejectResume)
			return 0;
		if (!paused)
			resumedGeneration = generation;
		return 1;
	}, (state) => reports.push(state));
	const check = (value, message) => {
		if (!value)
			throw new Error(message);
		checks.push(message);
	};
	const visible = (hidden) => {
		Object.defineProperty(document,
			'hidden',
			{value: hidden, configurable: true});
		document.dispatchEvent(new Event('visibilitychange'));
	};
	try {
		visible(true);
		owner.start();
		check(calls.join() === '1',
			'starting on a hidden page suspends immediately');
		document.dispatchEvent(new Event('visibilitychange'));
		check(calls.length === 1,
			'repeated visibility notifications do not repeat transitions');
		const loss = new Event('webglcontextlost', {cancelable: true});
		canvas.dispatchEvent(loss);
		check(loss.defaultPrevented,
			'context loss permits browser restoration');
		visible(false);
		check(calls.length === 1,
			'visibility alone cannot resume a lost context');
		// Model a backend listener installed after the host listener.
		canvas.addEventListener(
			'webglcontextrestored', () => generation++);
		canvas.dispatchEvent(new Event('webglcontextrestored'));
		check(calls.length === 1,
			'restoration waits until event listeners finish');
		await Promise.resolve();
		check(calls.join() === '1,0' && resumedGeneration === 1,
			'resume observes the backend restoration generation');
		window.dispatchEvent(new PageTransitionEvent('pagehide'));
		visible(true);
		window.dispatchEvent(new PageTransitionEvent('pageshow'));
		check(calls.join() === '1,0,1',
			'pageshow does not override hidden document state');
		rejectResume = true;
		visible(false);
		check(owner.paused && reports.at(-1).failed,
			'failed resume keeps the paused state and reports retry availability');
		rejectResume = false;
		owner.update();
		check(!owner.paused && !reports.at(-1).failed &&
				calls.join() === '1,0,1,0,0',
			'explicit retry resumes after a transient failure');
		owner.onHidden = () => owner.setPaused(true);
		visible(true);
		visible(false);
		check(owner.paused && owner.userPaused,
			'the frontend can retain a pause after returning to a visible page');
		owner.setPaused(false);
		check(!owner.paused,
			'explicit resume releases the retained pause');
		canvas.dispatchEvent(
			new Event('webglcontextlost', {cancelable: true}));
		window.dispatchEvent(new PageTransitionEvent('pagehide'));
		window.dispatchEvent(new PageTransitionEvent('pageshow'));
		canvas.dispatchEvent(new Event('webglcontextrestored'));
		await Promise.resolve();
		check(owner.paused && owner.userPaused && !owner.contextLost,
			'hiding during graphics recovery also retains the frontend pause');
		owner.setPaused(false);
		owner.onHidden = null;
		let online = true;
		owner.runHidden = () => online;
		visible(true);
		check(!owner.paused,
			'online sessions keep running in hidden tabs');
		owner.setPaused(true);
		check(owner.paused,
			'background policy does not override explicit pause');
		owner.setPaused(false);
		canvas.dispatchEvent(
			new Event('webglcontextlost', {cancelable: true}));
		check(owner.paused,
			'online sessions still suspend for graphics loss');
		canvas.dispatchEvent(new Event('webglcontextrestored'));
		await Promise.resolve();
		check(!owner.paused,
			'graphics recovery resumes hidden online sessions');
		window.dispatchEvent(new PageTransitionEvent('pagehide'));
		check(owner.paused,
			'navigation still suspends online sessions');
		window.dispatchEvent(new PageTransitionEvent('pageshow'));
		check(!owner.paused,
			'returning from navigation restores background policy');
		online = false;
		owner.update();
		check(owner.paused,
			'leaving a server while hidden restores normal suspension');
		visible(false);
		const transitions = calls.length;
		owner.stop();
		window.dispatchEvent(new PageTransitionEvent('pagehide'));
		canvas.dispatchEvent(new Event('webglcontextlost'));
		check(calls.length === transitions,
			'stopped hosts detach lifecycle listeners');
	} finally {
		owner.stop();
		delete document.hidden;
	}
	return {passed: true, checks};
}

run().catch((error) => ({passed: false, error: error.stack || String(error)}))
	.then((report) => fetch('/report',
		      {method: 'POST', body: JSON.stringify(report)}));
