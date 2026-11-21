#!/usr/bin/env python3
"""Exercise the connected Ghost chooser, indexed pixels and X11 navigation.

Rendering arithmetic is covered by the unit tests. These
private filename fixtures cover the frontend adapter, never race playback.
Installed files and capture-only settings must remain byte-for-byte intact.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from tests.checks.support.x11 import close_window
from tests.checks.support.images import bmp_rgb


def fixture(root, assets, names):
	root.mkdir()
	demos = root / 'Demos'
	demos.mkdir()

	for path in assets.resolve().iterdir():
		if path.name.lower() == 'game':
			# Keep the control source inside the fixture root. Prepared
			# resources require regular final components, including metadata.
			target = root / path.name
			target.mkdir()

			for child in path.iterdir():
				if child.is_file():
					shutil.copyfile(child, target / child.name)
				else:
					(target / child.name).symlink_to(child, target_is_directory=child.is_dir())
		elif path.name.lower() not in ('demos', 'userdata', 'original'):
			if path.is_file():
				shutil.copyfile(path, root / path.name)
			else:
				(root / path.name).symlink_to(path, target_is_directory=path.is_dir())

	for name in names:
		(demos / (name + '.MDE')).write_bytes(b'private filename-only fixture\n')

	return demos


def snapshot(directory):
	return {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in directory.iterdir()}


def check_handoffs(output):
	"""Observe the actual producer draw and Ghost entry, including reentry.

    Check producer/consumer wiring through the dispatcher and transition
    endpoint.
    """

	producer = ghost = None
	sources = []
	stale = 0

	for match in re.finditer(r'^Menu frames: (\S+) mix (\d+) clock (\d+):([\d ]+)$', output, re.M):
		page, mix, clock, words = match.groups()
		frames = tuple(map(int, words.split()))
		assert len(frames) == 32, match.group()

		if page in ('single', 'car'):
			producer = page, int(mix), int(clock), frames
		elif page == 'ghost':
			ghost = frames
		elif page == 'ghost-entry':
			assert producer is not None, 'missing predecessor draw'
			assert producer[1] == 256, ('missing exit endpoint', producer)
			assert frames == producer[3], (
				'Ghost did not inherit predecessor registers',
				producer,
				frames,
			)
			sources.append(producer[0])

			if ghost is not None:
				assert frames != ghost, 'fixture did not distinguish stale Ghost state'
				stale += 1

	assert sources == ['single', 'car', 'single'], sources
	assert stale == 2, stale


def live(command, root, assets):
	cases = [
		(['First', 'Second', 'Last'], 'First', ['--debug-page', 'single', '--debug-selection', '4'],
			['Return', 'Right', 'Down', 'Return', 'Escape', 'Escape', 'Return',
				'Right', 'Delete', 'Escape', 'Left', 'Down', 'Right'],
			'focus 2, mode 6, entries 3, page ghost', 'TimeAttack', None),
		([], 'Missing', ['--debug-page', 'ghost', '--debug-selection', '2'],
			['Right', 'Down', 'Return', 'Escape'], None, 'TimeAttack', ''),
		(['solo'], 'SOLO', ['--debug-page', 'ghost'], ['Down', 'Escape'],
			'focus 1, mode 6, entries 1, page single', 'GhostMode', 'solo'),
	]

	for name in ['semi;colon', 'double  space', ' leading', 'trailing ', 'r\u00e4ce']:
		chord = name == 'semi;colon'
		cases.append(([name], 'Previous', ['--debug-page', 'ghost', '--debug-selection', '0' if chord else '2',
			'--debug-confirmation', '0'],
			[('Right', 'Delete')] if chord else [],
			'focus 2, mode 3, entries 1, page ghost', 'GhostMode',
			name if name == 'r\u00e4ce' else 'Previous'))

	for number, (names, saved, arguments, keys, state, mode, wanted) in enumerate(cases):
		game = root / f'live-game-{number}'
		demos = fixture(game, assets, names)
		before = snapshot(demos)
		order = [p.stem for p in demos.iterdir()]

		if wanted is None:
			wanted = order[min(order.index(saved) + 1, len(order) - 1)]

		cfg = root / f'live-{number}.cfg'
		cfg.write_text('CarHorn Honker1\nGameType GhostMode\nDemoFileName ' + saved + '\n')
		log = root / f'live-{number}.log'

		with log.open('wb') as stream:
			process = subprocess.Popen(
				command + ['--assets', str(game), '--user-data', str(root / 'user'), '--debug-settings', str(cfg),
					'--debug-window-titles', '1',
					'--debug-menu-frames', '1'] + arguments,
				env=dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy'),
				stdout=stream,
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
					chord = list(key) if isinstance(key, tuple) else [key]
					subprocess.run(['xdotool', 'keydown'] + chord, check=True, timeout=10)

					try:
						time.sleep(.04)
					finally:
						subprocess.run(['xdotool', 'keyup'] + chord, check=True, timeout=10)

					time.sleep(.4)

				close_window(window)
				assert process.wait(timeout=15) == 0, log.read_text()
				output = log.read_text()

				if state:
					assert 'Ghost setup: ' + state in output, output

				if number == 0:
					assert 'Car setup:' in output and 'entries 1, leaves 1' in output, output
					check_handoffs(output)

				values = dict(line.split(None, 1) if len(line.split(None, 1)) == 2 else (line.strip(), '')
					for line in cfg.read_text().splitlines() if line and not line.startswith('//'))
				assert values['DemoFileName'] == wanted, (number, values['DemoFileName'], wanted)
				assert values['GameType'] == mode, (number, values['GameType'], mode)
				assert snapshot(demos) == before, 'installed recording changed'
			finally:
				if process.poll() is None:
					process.terminate()

					try:
						process.wait(timeout=5)
					except subprocess.TimeoutExpired:
						process.kill()
						process.wait(timeout=5)

	print(f'{len(cases)} X11 Ghost flows pass: shared frame handoffs, selection/save, car return, mode return, empty list, protected files and CFG name limits')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--live', action='store_true')
	parser.add_argument('--report', type=Path)
	parser.add_argument('--compare', type=Path)
	args = parser.parse_args()
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0', '--debug-window-titles', '1']
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-ghost-preview-') as temporary:
		root = Path(temporary)
		env = dict(os.environ)
		cases = [([], ''), (['JeePen'], 'JeePen'),
			(['Race%02u' % i for i in range(20)], 'Race19'), (['x' * 63], 'x' * 63)]

		for number, (names, saved) in enumerate(cases):
			game = root / f'game-{number}'
			demos = fixture(game, args.gamedir, names)
			before = snapshot(demos)
			cfg = root / f'{number}.cfg'
			cfg.write_text('CarHorn Honker1\nGameType GhostMode\nDemoFileName ' + saved + '\n')
			cfg_before = cfg.read_bytes(), cfg.stat().st_mtime_ns

			for focus in range(3):
				for frames in (2, 120):
					bmp, raw = root / 'frame.bmp', root / 'frame.idx'
					output = subprocess.run(
						command + ['--assets', str(game), '--user-data', str(root / 'user'), '--debug-settings', str(cfg),
							'--debug-page', 'ghost', '--debug-selection', str(focus), '--debug-frames', str(frames),
							'--debug-capture', str(bmp), '--debug-indexed', str(raw)],
						env=env,
						capture_output=True,
						text=True,
						timeout=60,
					)
					assert output.returncode == 0, output
					actual_focus = 0 if not names and focus == 2 else focus
					mode = 6 if actual_focus == 1 else 3
					assert f'Ghost setup: focus {actual_focus}, mode {mode}, entries 1, page ghost' in output.stdout
					indices = raw.read_bytes()
					rgb = bmp_rgb(bmp)
					assert len(indices) == 256768
					assert rgb == b''.join(indices[n * 3:n * 3 + 3] for n in indices[768:])
					assert cfg_before == (cfg.read_bytes(), cfg.stat().st_mtime_ns)
					assert snapshot(demos) == before
					results.append(dict(case=number, focus=focus, frames=frames,
						rgb_sha256=hashlib.sha256(rgb).hexdigest()))

		assert not list((root / 'user').glob('*.cfg')), 'capture wrote settings'

		if args.live:
			live(command, root, args.gamedir)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'Ghost pixels differ across builds/backends'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} Ghost captures pass: focus, scrolling, empty/long names, indexed presentation and source preservation')


if __name__ == '__main__':
	main()
