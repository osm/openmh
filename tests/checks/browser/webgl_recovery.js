'use strict';

const canvas = document.getElementById('canvas');
const checks = [], errors = [];

function event(name, action) {
	return new Promise((resolve, reject) => {
		const complete = () => {
			clearTimeout(timeout);
			resolve();
		};
		const timeout = setTimeout(() => {
			canvas.removeEventListener(name, complete);
			reject(new Error('No ' + name + ' event'));
		}, 5000);
		canvas.addEventListener(name, complete, {once: true});
		action();
	});
}

async function run() {
	const game = await createRecoveryCheck(
		{canvas, printErr: (text) => errors.push(text)});
	const step = (number, label) => {
		if (game._mh_webgl_canvas_recovery(number))
			throw new Error(label + ': ' + errors.join('\n'));
		checks.push(label);
	};
	try {
		step(0, 'initial canvas matches all independent pixel expectations');
		const gl = canvas.getContext('webgl2');
		const loss = gl.getExtension('WEBGL_lose_context');
		if (!loss)
			throw new Error('WEBGL_lose_context is unavailable');
		for (let cycle = 0; cycle < 3; cycle++) {
			await event(
				'webglcontextlost', () => loss.loseContext());
			step(1, 'lost canvas rejects drawing, cycle ' + cycle);
			if (cycle === 2)
				step(4, 'canvas closes safely while context is lost');
			await new Promise(
				(resolve) => setTimeout(resolve, 100));
			await event('webglcontextrestored',
				() => loss.restoreContext());
			if (cycle === 0)
				step(2, 'live canvas restores every pixel');
			if (cycle === 1)
				step(3, 'restored owner can close before drawing and be replaced');
			if (cycle === 2)
				step(5, 'new owner acquires the restored window');
			step(2,
				'subsequent draw remains correct, cycle ' +
					cycle);
		}
		step(6, 'restored canvas resizes and retains all pixels');
	} finally {
		step(7, 'recovery resources close cleanly');
	}
	await fetch('/report',
		{method: 'POST', body: JSON.stringify({passed: true, checks})});
}

run().catch((error) => fetch('/report', {
	method: 'POST',
	body: JSON.stringify(
		{passed: false, checks, error: error.stack || String(error)})
}));
