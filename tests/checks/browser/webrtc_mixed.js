(async function() {
	let runtime;
	const diagnostics = [];
	try {
		runtime = await createMixedClientCheck({
			print: text => diagnostics.push(text),
			printErr: text => diagnostics.push(text)
		});
		let response = await fetch('/input', {
			method: 'POST',
			body: JSON.stringify(
				{start: true, origin: location.origin})
		});
		let result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		const config = await (await fetch('/config')).json();
		if (!runtime.ccall('mh_mixed_client_open',
			    'number',
			    ['string', 'number'],
			    [config.endpoint, 0])) {
			throw new Error('Cannot open browser game session');
		}
		const deadline = performance.now() + 35000;
		while (true) {
			const state = runtime.ccall(
				'mh_mixed_client_step', 'number', [], []);
			if (state < 0 || performance.now() >= deadline) {
				throw new Error('Browser mixed race failed');
			}
			if (state === 1) {
				break;
			}
			await new Promise(resolve => setTimeout(resolve, 5));
		}
		response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({verify: true})});
		result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		await fetch('/report',
			{method: 'POST', body: JSON.stringify({passed: true})});
	} catch (error) {
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify({
				passed: false,
				error: String(error),
				diagnostics
			})
		});
	} finally {
		if (runtime) {
			runtime.ccall('mh_mixed_client_close', null, [], []);
		}
	}
})();
