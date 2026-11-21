(async function() {
	let runtime;
	try {
		const config = await (await fetch('/config')).json();
		runtime = await createBrowserTransportCheck();
		if (!runtime.ccall('mh_browser_transport_check_start',
			    'number',
			    ['string', 'string'],
			    [
				    config.signaling + '/0',
				    config.signaling + '/1'
			    ])) {
			throw new Error('Cannot start browser transports');
		}
		const deadline = performance.now() + 20000;
		while (true) {
			const result =
				runtime.ccall('mh_browser_transport_check_step',
					'number',
					[],
					[]);
			if (result < 0 || performance.now() >= deadline) {
				throw new Error(
					'Browser transport exchange failed');
			}
			if (result === 1) {
				break;
			}
			await new Promise(resolve => setTimeout(resolve, 1));
		}
		const response = await fetch('/input',
			{method: 'POST', body: JSON.stringify({verify: true})});
		const result = await response.json();
		if (!result.ok) {
			throw new Error(result.error);
		}
		await fetch('/report',
			{method: 'POST', body: JSON.stringify({passed: true})});
	} catch (error) {
		await fetch('/report', {
			method: 'POST',
			body: JSON.stringify(
				{passed: false, error: String(error)})
		});
	} finally {
		if (runtime) {
			runtime.ccall('mh_browser_transport_check_close',
				null,
				[],
				[]);
		}
	}
})();
