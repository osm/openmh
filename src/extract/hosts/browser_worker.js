'use strict';

importScripts('extract-workspace.js', 'openmotorex.js');

function copyFile(module, source, destination, report) {
	const total = module.FS.stat(source).size;
	let completed = 0;
	const input = module.FS.open(source, 'r');
	let output;
	const buffer = new Uint8Array(1024 * 1024);
	try {
		output = module.FS.open(destination, 'w');
		report({stage: 'prepare', operation: 'copy', completed, total});
		for (;;) {
			const count =
				module.FS.read(input, buffer, 0, buffer.length);
			if (!count)
				break;
			module.FS.write(output, buffer, 0, count);
			completed += count;
			report({
				stage: 'prepare',
				operation: 'copy',
				completed,
				total
			});
		}
	} finally {
		module.FS.close(input);
		if (output)
			module.FS.close(output);
	}
}

async function exportPackage(module, directory, name, report) {
	const total = module.FS.stat('/work/' + name).size;
	const file = await directory.getFileHandle(name, {create: true});
	const handle = await file.createSyncAccessHandle();
	let input;
	const buffer = new Uint8Array(1024 * 1024);
	let at = 0;
	try {
		input = module.FS.open('/work/' + name, 'r');
		report({
			stage: 'export',
			operation: 'export',
			completed: 0,
			total
		});
		for (;;) {
			const count =
				module.FS.read(input, buffer, 0, buffer.length);
			if (!count)
				break;
			if (handle.write(buffer.subarray(0, count), {at}) !==
				count)
				throw new Error('Incomplete package export.');
			at += count;
			report({
				stage: 'export',
				operation: 'export',
				completed: at,
				total
			});
		}
		handle.truncate(at);
		handle.flush();
	} finally {
		if (input)
			module.FS.close(input);
		handle.close();
	}
	return file.getFile();
}

// Limit browser messages while preserving operation changes and completion.
function progressReporter() {
	let previous = '', last = 0;
	return (progress) => {
		const key = [
			progress.stage,
			progress.operation,
			progress.detail || ''
		].join('/');
		const now = performance.now();
		if (key === previous && now - last < 100 &&
			!(progress.total > 0 &&
				progress.completed === progress.total))
			return;
		previous = key;
		last = now;
		postMessage({type: 'progress', progress});
	};
}

async function prepare(job, directory, handle) {
	let diagnostic = '';
	const report = progressReporter();
	report({
		stage: 'prepare',
		operation: 'starting',
		completed: 0,
		total: 0
	});
	const module = await createOpenMHExtractor({
		print: () => {},
		onProgress: (progress) => report({
			...progress,
			stage: ['write-package', 'convert'].includes(
				       progress.operation)
				? 'build'
				: 'prepare'
		}),
		printErr: (message) => {
			diagnostic = message;
		}
	});
	const workspace = new OpenMHWorkspace(module, handle);
	workspace.mount('/work');
	module.FS.mkdir('/input');
	module.FS.mount(module.WORKERFS, {files : job.files}, '/input');
	const options = [];
	if (job.compact)
		options.push('--compact');
	if (job.excludeMusic)
		options.push('--exclude-music');
	if (job.excludeMovies)
		options.push('--exclude-movies');
	const run = (sources, destination = 'game.ass') => {
		diagnostic = '';
		const result = module.callMain([
			...options,
			...sources.map((source) => '/input/' + source),
			'/work/' + destination
		]);
		if (result !== 0)
			throw new Error(workspace.failure || diagnostic ||
				'Could not prepare this game.');
	};

	const combined = job.combinePatch && job.patch;
	if (job.source.toLowerCase().endsWith('.ass')) {
		copyFile(module,
			'/input/' + job.source,
			'/work/game.ass',
			report);
		if (combined)
			run([job.patch]);
	} else
		run(combined ? [job.source, job.patch] : [job.source]);
	if (job.patch && !combined)
		run([job.patch], 'patch30.ass');
	const files =
		[await exportPackage(module, directory, 'game.ass', report)];
	if (job.patch && !combined)
		files.push(await exportPackage(
			module, directory, 'patch30.ass', report));
	for (const file of job.files)
		if (/\.ass$/i.test(file.name) && file.name !== job.source)
			files.push(file);
	if (job.patch || files.length > 1) {
		report({
			stage: 'export',
			operation: 'validate',
			completed: 0,
			total: 0
		});
		module.FS.mkdir('/validation');
		module.FS.mount(module.WORKERFS,
			{
				files : files.map(
					(file, index) => new File([file],
						'package' + index + '.ass'))
			},
			'/validation');
		try {
			if (module._mh_browser_check_packages() !== 0)
				throw new Error(diagnostic ||
					'The prepared packages cannot be used together.');
		} finally {
			module.FS.unmount('/validation');
		}
	}
	return files.length === 1 ? files[0] : files;
}

self.onmessage = async ({data: job}) => {
	let handle;
	try {
		const root = await navigator.storage.getDirectory();
		const directory = await root.getDirectoryHandle(
			'openmh-extractor', {create: true});
		const backing = await directory.getFileHandle(
			'workspace', {create: true});
		handle = await backing.createSyncAccessHandle();
		handle.truncate(0);
		const file = await prepare(job, directory, handle);
		postMessage({type: 'complete', file});
	} catch (error) {
		postMessage({
			type: 'error',
			message: error.name === 'QuotaExceededError'
				? 'Browser storage is full. Free some space and try again.'
				: error.message || String(error)
		});
	} finally {
		handle?.close();
	}
};
