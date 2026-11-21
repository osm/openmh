#!/usr/bin/env python3
"""Check installed car artwork, indexed presentation and X11 page/horn input.

Rendering arithmetic is covered by the unit tests.
This covers real CAR metadata/LOBs/thumbnails, catalog mapping, disc marker,
shared page state and the presentation/audio adapters. Unlocks are fixtures.
"""

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.x11 import close_window
from tests.checks.support.images import bmp_rgb


ORDER = [2, 7, 8, 3, 5, 9, 1, 6, 10, 4]


def live(command, root):
	cases = [
		([], ['Return', 'Right', 'Return', 'Return', 'Right', 'Down', 'Left',
			'Down', 'Return', 'Return', 'Down', 'Right', 'Escape', 'Return'],
			'selection 3, car 6, index 7, automatic 0, record 1, horn Honker1, previews 2, '
			'visible 10, unlocked 10, entries 2, leaves 1, page car'),
		(['--debug-page', 'car', '--debug-car', '4', '--debug-unlocked-cars', '3'],
			['Return', 'Escape', 'Return'],
			'selection 0, car 8, index 2, automatic 1, record 0, horn Honker1, previews 0, '
			'visible 10, unlocked 3, entries 2, leaves 1, page car'),
		(['--debug-page', 'setup', '--debug-mode', '2', '--debug-car', '4', '--debug-special-cars', '0'],
			['Return', 'Right', 'Down', 'Down', 'Return', 'Escape', 'Return'],
			'selection 2, car 10, index 8, automatic 1, record 0, horn Honker1, previews 1, '
			'visible 9, unlocked 10, entries 2, leaves 1, page car'),
		(['--debug-page', 'car', '--debug-selection', '2'], ['Right', 'Return', 'Left', 'Return'],
			'selection 2, car 1, index 6, automatic 1, record 0, horn Honker1, previews 2, '
			'visible 10, unlocked 10, entries 1, leaves 0, page car'),
	]

	for index, (arguments, keys, expected) in enumerate(cases):
		log = root / f'live-{index}.log'

		with log.open('wb') as output:
			process = subprocess.Popen(
				command + ['--debug-special-cars', '1'] + arguments,
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
				assert 'Car setup: ' + expected in log.read_text(), log.read_text()
				assert 'Exit page completed' not in log.read_text(), log.read_text()
			finally:
				if process.poll() is None:
					process.terminate()
					process.wait(timeout=10)

	print('Four X11 car flows passed: retained edits, locked/special clamps, horn preview and reentry')


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
	cases = [dict(car=car, mode=mode, selection=row, frames=frames) for car in range(1, 11)
		for mode in range(4) for row in range(4) for frames in ((2, 120) if mode == 0 else (120,))]
	cases += [dict(car=car, selection=0, frames=frames, unlocked_cars=1)
		for car in (2, 4, 10) for frames in (1, 10, 120)]
	cases += [dict(car=4, special_cars=special, mode=mode, selection=1, automatic=0, record=1)
		for special in (0, 1) for mode in range(4)]

	for path in sorted((args.gamedir / 'Sounds/CarHorns').glob('*'), key=lambda p: p.name.lower()):
		if path.suffix.lower() == '.chf':
			name = next(line.split('//')[0].split(None, 1)[1].strip() for line in path.read_text().splitlines()
				if line.split() and line.split()[0].lower() == 'name')
			cases.append(dict(car=1, selection=2, horn=name))

	results = []

	with tempfile.TemporaryDirectory(prefix='mh-car-preview-') as temporary:
		root = Path(temporary)
		command += ['--user-data', str(root / 'userdata')]

		for overrides in cases:
			case = dict(car=1, mode=0, selection=0, frames=120, unlocked_cars=10,
				special_cars=1, automatic=1, record=0, horn='Honker1')
			case.update(overrides)
			capture, raw = root / 'screen.bmp', root / 'screen.idx'
			arguments = [part for key, value in case.items() for part in ('--debug-' + key.replace('_', '-'), str(value))]
			output = subprocess.run(
				command + ['--debug-page', 'car'] + arguments + ['--debug-capture', str(capture), '--debug-indexed', str(raw)],
				capture_output=True,
				text=True,
				timeout=60,
			)
			assert output.returncode == 0, (case, output.stdout, output.stderr)
			indices = raw.read_bytes()
			assert len(indices) == 256768
			expected = b''.join(indices[i * 3:i * 3 + 3] for i in indices[768:])
			actual = bmp_rgb(capture)
			assert actual == expected, (case, 'indexed presentation mismatch')
			visible = 9 if case['mode'] == 1 or not case['special_cars'] else 10
			index = min(ORDER.index(case['car']), visible - 1)
			state = (f"Car setup: selection {case['selection']}, car {ORDER[index]}, index {index}, "
				f"automatic {case['automatic']}, record {case['record']}, horn {case['horn']}, previews 0, "
				f"visible {visible}, unlocked {case['unlocked_cars']}, entries 1, leaves 0, page car")
			assert state in output.stdout, (case, output.stdout)
			results.append(dict(case=case, rgb_sha256=hashlib.sha256(actual).hexdigest(), state=output.stdout.strip()))

		settled = [r for r in results[:200] if r['case']['frames'] == 120
			and r['case']['selection'] == 0 and r['case']['mode'] == 0]
		assert len({r['rgb_sha256'] for r in settled}) == 10

		# Portable disc marker follows the original size comparison. Contents
		# do not matter; test correct size, off by one, then absent path.
		marker = root / 'marker-menu/Motor.Bmp'
		marker.parent.mkdir(parents=True)
		game = root / 'marker-installation'
		prepare_view(game, args.gamedir, {'Menu': marker.parent})

		for size, expected_car in ((758696, 4), (758695, 10), (None, 10)):
			if size is None:
				marker.unlink()
			else:
				with marker.open('wb') as f:
					f.truncate(size)

			output = subprocess.run(
				command + ['--assets', str(game), '--debug-page', 'car', '--debug-car', '4',
					'--debug-capture', str(root / 'disc.bmp')],
				capture_output=True,
				text=True,
				timeout=60,
			)
			assert output.returncode == 0 and f'Car setup: selection 0, car {expected_car},' in output.stdout, output

		if args.live:
			live(command, root)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build/presentation mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} car captures matched every indexed/palette pixel; all horns and disc marker cases passed')


if __name__ == '__main__':
	main()
