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
	let label = '';
	const game = await createRendererCheck({
		canvas,
		print: (text) => checks.push(label + ': ' + text),
		printErr: (text) => errors.push(text)
	});
	const step = async (number, profile) => {
		if (await game.ccall('mh_webgl_renderer_recovery',
			    'number',
			    ['number', 'number'],
			    [number, profile],
			    {async: true}))
			throw new Error(label + ': ' + errors.join('\n'));
	};
	try {
		for (let profile = 0; profile < 3; profile++) {
			label = ['Glide', 'Direct3D', 'PowerVR'][profile];
			await step(0, profile);
			const gl = canvas.getContext('webgl2');
			const loss = gl.getExtension('WEBGL_lose_context');
			if (!loss)
				throw new Error(
					'WEBGL_lose_context is unavailable');
			for (let cycle = 0; cycle < 2; cycle++) {
				await event('webglcontextlost',
					() => loss.loseContext());
				await step(1, profile);
				await new Promise(
					(resolve) => setTimeout(resolve, 100));
				await event('webglcontextrestored',
					() => loss.restoreContext());
				if (cycle === 1)
					await step(3, profile);
				await step(2, profile);
				await step(4, profile);
			}
			// Close both before a restored draw and while loss is active.
			for (const closeDuringLoss of [false, true]) {
				await event('webglcontextlost',
					() => loss.loseContext());
				await step(1, profile);
				if (closeDuringLoss)
					await step(5, profile);
				await new Promise(
					(resolve) => setTimeout(resolve, 100));
				await event('webglcontextrestored',
					() => loss.restoreContext());
				if (!closeDuringLoss)
					await step(5, profile);
				await step(0, profile);
				checks.push(label + ': owner replacement ' +
					(closeDuringLoss
							? 'during loss'
							: 'before restored drawing'));
			}
			await step(5, profile);
		}
	} finally {
		await step(6, 0);
	}
	await fetch('/report',
		{method: 'POST', body: JSON.stringify({passed: true, checks})});
}

run().catch((error) => fetch('/report', {
	method: 'POST',
	body: JSON.stringify(
		{passed: false, checks, error: error.stack || String(error)})
}));
