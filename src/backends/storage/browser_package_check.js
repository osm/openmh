'use strict';

importScripts('openmotor.js');

self.onmessage = async ({data: files}) => {
	try {
		let diagnostic = '';
		const engine = await createOpenMH(
			{printErr: (message) => diagnostic = message});
		engine.FS.mkdir('/validation');
		engine.FS.mount(engine.WORKERFS, {files}, '/validation');
		if (engine._mh_browser_check_packages() !== 0)
			throw new Error(diagnostic ||
				'These packages cannot be used together.');
		postMessage({});
	} catch (error) {
		postMessage({error: error.message || String(error)});
	}
};
