#!/usr/bin/env python3
"""Exercise the standalone browser API with a minimal test page."""

import argparse
from pathlib import Path

from tests.checks.support.browser import run_browser
from tests.checks.support.build import build_executable


def resources(build, assets):
	target = build_executable(build, 'mh-browser-single-player-check')
	names = ['database.js', 'package-database.js', 'storage.js', 'lifecycle.js', 'checkpoints.js',
		'save-session.js', 'race-history.js', 'multiplayer.js', 'preferences.js', 'player.js', 'session.js']
	content = {'/openmotor.js': (target.read_bytes(), 'text/javascript'),
		'/' + target.with_suffix('.wasm').name:
		(target.with_suffix('.wasm').read_bytes(), 'application/wasm'),
		'/game.ass': (assets.read_bytes(), 'application/octet-stream')}

	for name in [*names, 'package-check.js']:
		content['/' + name] = ((build / name).read_bytes(), 'text/javascript')

	for name in ['browser_flow.js', 'browser_frontend_api.js']:
		content['/' + name] = (Path(__file__).with_name(name).read_bytes(), 'text/javascript')

	scripts = ['openmotor.js', *names, 'browser_flow.js', 'browser_frontend_api.js']
	page = '<!doctype html><meta charset="utf-8"><canvas id="canvas" tabindex="0"></canvas><p id="error" hidden></p>'
	page += ''.join(f'<script src="/{name}"></script>' for name in scripts)
	content['/'] = (page.encode(), 'text/html')

	return content


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--assets', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--browser', choices=('chrome', 'firefox'), required=True)
	parser.add_argument('--executable')
	args = parser.parse_args()
	report = run_browser(resources(args.build, args.assets), args.output, args.browser,
		args.executable, timeout=600)

	if not report['passed']:
		raise RuntimeError(report['error'])

	print(f'{args.browser}: {len(report["checks"])} browser API checks passed')


if __name__ == '__main__':
	main()
