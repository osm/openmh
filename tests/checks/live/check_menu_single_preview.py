#!/usr/bin/env python3
"""Check native single-player captures and X11 main/single page transitions.

Rendering arithmetic is covered by the unit tests.
This tool exercises installed catalogs, SDL presentation and live callbacks.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from tests.checks.support.x11 import close_window
from tests.checks.support.images import bmp_rgb


def live(command, root, assets):
	demo_dirs = [p for p in assets.iterdir() if p.name.lower() == 'demos']
	have_demos = any(p.name.lower().endswith('.mde') for d in demo_dirs for p in d.iterdir())
	log = root / 'live.log'

	with log.open('wb') as output:
		process = subprocess.Popen(
			command,
			env=dict(os.environ, SDL_VIDEO_DRIVER='x11'),
			stdout=output,
			stderr=subprocess.STDOUT,
		)

		try:
			window = subprocess.check_output(
				['xdotool', 'search', '--sync', '--pid', str(process.pid),
					'--name', 'Motorhead main-menu preview'],
				timeout=30,
			).splitlines()[-1].decode()
			subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True, timeout=10)
			time.sleep(.6)

			# Commit League, Single, Ghost on successive Escape departures;
			# re-enter with the retained selection and update demo availability.
			keys = ['Return', 'Right', 'Right', 'Escape', 'Return', 'Left', 'Escape',
				'Return', 'Right', 'Right', 'Right', 'Escape', 'Return', 'Right']

			for key in keys:
				assert process.poll() is None, log.read_text()
				subprocess.run(['xdotool', 'keydown', key], check=True, timeout=10)

				try:
					time.sleep(.04)
				finally:
					subprocess.run(['xdotool', 'keyup', key], check=True, timeout=10)

				time.sleep(.4)

			assert process.poll() is None, log.read_text()
			close_window(window)
			assert process.wait(timeout=10) == 0, log.read_text()
			text = log.read_text()
			assert re.search(r'Menu preview: selection 0,', text), text
			mode = 3 if have_demos else 2
			assert re.search(rf'Single player: selection 4, mode {mode}, car \d+, entries 4, leaves 3, page single', text), text
			assert 'Dialog:' not in text and 'Exit page completed' not in text, text
		finally:
			if process.poll() is None:
				process.terminate()
				process.wait(timeout=10)

	print('X11 main/single transitions, retained selection, departure modes and input ownership passed')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--live', action='store_true')
	parser.add_argument('--audio', action='store_true')
	parser.add_argument('--report', type=Path)
	parser.add_argument('--compare', type=Path)
	args = parser.parse_args()
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-no-settings',
		*(['--debug-require-audio'] if args.audio else ['--audio', '0']), '--debug-window-titles', '1']
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-single-preview-') as temporary:
		root = Path(temporary)
		command += ['--user-data', str(root / 'userdata')]

		for mode in range(3):
			for selected in range(5):
				for frames in (1, 2, 10, 120):
					capture, raw = root / 'screen.bmp', root / 'screen.idx'
					output = subprocess.run(
						command + ['--debug-page', 'single', '--debug-mode', str(mode),
							'--debug-selection', str(selected), '--debug-car', '10', '--debug-track', '8',
							'--debug-capture', str(capture), '--debug-indexed', str(raw), '--debug-frames', str(frames)],
						capture_output=True,
						text=True,
						timeout=60,
					)
					assert output.returncode == 0, (
						mode,
						selected,
						frames,
						output.stdout,
						output.stderr,
					)
					indices = raw.read_bytes()
					assert len(indices) == 256768
					expected = b''.join(indices[i * 3:i * 3 + 3] for i in indices[768:])
					actual = bmp_rgb(capture)
					assert actual == expected, (
						mode,
						selected,
						frames,
						'presentation differs from indexed output',
					)
					assert re.search(rf'Single player: selection {selected}, mode {mode}, car \d+, entries 1, leaves 0, page single', output.stdout)
					results.append({'mode': mode, 'selection': selected, 'frames': frames,
						'rgb_sha256': hashlib.sha256(actual).hexdigest(), 'state': output.stdout.strip()})

		assert len({r['rgb_sha256'] for r in results if r['frames'] == 1}) == 1
		assert len({r['rgb_sha256'] for r in results if r['frames'] == 120}) == 15

		if args.live:
			live(command, root, args.gamedir)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build/presentation mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} single-player captures matched all 256,000 indexed/palette pixels')


if __name__ == '__main__':
	main()
