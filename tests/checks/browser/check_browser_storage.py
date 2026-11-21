#!/usr/bin/env python3
"""Check real browser saves and backups in an isolated Chrome or Firefox profile.

Requires a Wasm build or packaged runtime and permission to bind a loopback server.
Use an output directory visible to the browser (snap Firefox cannot see /tmp).
No game assets or existing browser saves are used.
"""

import argparse
from pathlib import Path

from tests.checks.support.build import build_executable
from tests.checks.support.browser import run_browser


def resources(directory, executable):
	files = {'/openmotor.js': executable, '/openmotor.wasm': executable.with_suffix('.wasm')}

	for name in ('database.js', 'package-database.js', 'storage.js', 'backup.js', 'race-history.js', 'checkpoints.js', 'save-session.js'):
		files['/' + name] = directory / name

	files['/check.js'] = Path(__file__).with_name('browser_storage.js')
	result = {url: (path.read_bytes(), 'application/wasm' if url.endswith('.wasm')
		else 'text/javascript') for url, path in files.items()}
	scripts = ''.join(f'<script src="{url}"></script>' for url in files if url.endswith('.js'))
	result['/'] = (('<!doctype html><meta charset="utf-8">' + scripts).encode(), 'text/html')

	return result


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	source = parser.add_mutually_exclusive_group()
	source.add_argument('--build', type=Path, default=Path('build/wasm'))
	source.add_argument('--runtime', type=Path, help='packaged browser-runtime directory')
	parser.add_argument('--output', type=Path, required=True, help='new output directory')
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	parser.add_argument('--executable', help='browser executable override')
	args = parser.parse_args()
	directory = (args.runtime or args.build).resolve()
	executable = directory / 'openmotor.js' if args.runtime else build_executable(directory, 'openmotor')
	content = resources(directory, executable)
	root = args.output.resolve()
	report = run_browser(content, root, args.browser, args.executable)

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: {len(report["checks"])} browser storage checks passed; '
		f'report: {root}/report.json')


if __name__ == '__main__':
	main()
