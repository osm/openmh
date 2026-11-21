#!/usr/bin/env python3
"""Check installed track setup captures and X11 page/input integration.

Rendering arithmetic is covered by the unit tests.
This checks asset paths, shared outline fitting, indexed SDL presentation,
retained setup values, Time attack row bounds and locked-track return.
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


def live(command, root):
	cases = [
		([], ['Return', 'Right', 'Return', 'Right', 'Down', 'Right', 'Down',
			'Right', 'Escape', 'Return'],
			'selection 2, track 3, laps 10, catch-up 1, unlocked 8, entries 2, leaves 1, page setup'),
		(['--debug-page', 'setup', '--debug-mode', '2'], ['Down', 'Down', 'Right', 'Escape',
			'Right', 'Right', 'Right', 'Return', 'Down'],
			'selection 1, track 1, laps 10, catch-up 0, unlocked 8, entries 2, leaves 1, page setup'),
		(['--debug-page', 'setup', '--debug-track', '5', '--debug-unlocked-tracks', '3'],
			['Return', 'Escape', 'Right', 'Return'],
			'selection 0, track 3, laps 5, catch-up 0, unlocked 3, entries 2, leaves 1, page setup'),
	]

	for index, (arguments, keys, expected) in enumerate(cases):
		log = root / f'live-{index}.log'

		with log.open('wb') as output:
			process = subprocess.Popen(
				command + arguments,
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
				assert 'Track setup: ' + expected in log.read_text(), log.read_text()
				assert 'Exit page completed' not in log.read_text(), log.read_text()
			finally:
				if process.poll() is None:
					process.terminate()
					process.wait(timeout=10)

	print('Three X11 setup flows passed: retained track/laps/CatchUp, Time attack bounds and locked-track return')


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
		'--debug-unlocked-tracks', '8', '--debug-unlocked-cars', '10',
		*(['--debug-require-audio'] if args.audio else ['--audio', '0']), '--debug-window-titles', '1']
	cases = [(track, mode, row, frames, 8) for track in range(1, 9)
		for mode in (0, 2) for row in range(3) for frames in (2, 120)]
	cases += [(track, 0, 0, frames, 1) for track in (2, 5) for frames in (1, 10, 120)]
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-race-preview-') as temporary:
		root = Path(temporary)
		command += ['--user-data', str(root / 'userdata')]

		for track, mode, row, frames, unlocked in cases:
			capture, raw = root / 'screen.bmp', root / 'screen.idx'
			output = subprocess.run(
				command + ['--debug-page', 'setup', '--debug-track', str(track), '--debug-mode', str(mode),
					'--debug-selection', str(row), '--debug-catch-up', '1', '--debug-unlocked-tracks', str(unlocked),
					'--debug-capture', str(capture), '--debug-indexed', str(raw), '--debug-frames', str(frames)],
				capture_output=True,
				text=True,
				timeout=60,
			)
			assert output.returncode == 0, (track, mode, row, frames, output.stdout, output.stderr)
			indices = raw.read_bytes()
			assert len(indices) == 256768
			expected = b''.join(indices[i * 3:i * 3 + 3] for i in indices[768:])
			actual = bmp_rgb(capture)
			assert actual == expected, (track, mode, row, frames, 'indexed presentation mismatch')
			assert re.search(rf'Track setup: selection {row}, track {track}, laps 5, catch-up 1, '
				rf'unlocked {unlocked}, entries 1, leaves 0, page setup', output.stdout), output.stdout
			results.append(dict(track=track, mode=mode, row=row, frames=frames, unlocked=unlocked,
				rgb_sha256=hashlib.sha256(actual).hexdigest(), state=output.stdout.strip()))

		# Every installed track has distinct settled artwork in both modes.
		for mode in (0, 2):
			assert len({r['rgb_sha256'] for r in results
				if r['frames'] == 120 and r['row'] == 0 and r['mode'] == mode and r['unlocked'] == 8}) == 8

		if args.live:
			live(command, root)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build/presentation mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} track setup captures matched all 256,000 indexed/palette pixels')


if __name__ == '__main__':
	main()
