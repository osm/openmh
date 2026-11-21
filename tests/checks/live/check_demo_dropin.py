#!/usr/bin/env python3
"""Check demo resource providers using supplied installation directories.

No openmotorex conversion is performed. --live adds copied-client menu/race/screenshot,
return, settings and relaunch checks under Xvfb. Original files are hash checked
before and after; only openmotor and userdata may be added to each installation.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from tests.checks.support.build import build_executable
from tests.checks.live.check_demo_menus_live import stop
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def originals(root):
	return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
		for p in root.rglob('*') if p.is_file() and p.name != 'openmotor'
		and 'userdata' not in p.relative_to(root).parts}


def run_check(command, log):
	result = subprocess.run([str(p) for p in command], capture_output=True,
		text=True, timeout=120)
	log.write_text(result.stdout + result.stderr)
	assert result.returncode == 0, log.read_text()


def check_assets(build, game, prepared, reports):
	run_check([build_executable(build, 'mh-demo-sources'), game, prepared],
		reports / (game.name + '-providers.log'))
	profiles = ('software', 'glide', 'direct3d', 'powervr') if game.name == 'motordemo_mplayer' else (
		'software' if 'soft' in game.name else 'glide',)
	tracks = 1 if game.name == 'motordemo_mplayer' else 2
	count = 0

	for profile in profiles:
		for car in range(1, 4):
			for track in range(1, tracks + 1):
				run_check([build_executable(build, 'mh-demo-session-check'), game,
					profile, car, track],
					reports / f'{game.name}-{profile}-{car}-{track}.log')
				count += 1

	# Complete/restart the normal simulation on every available course.
	for track in range(1, tracks + 1):
		run_check([build_executable(build, 'mh-demo-session-check'), game,
			profiles[0], 1, track, '--finish'],
			reports / f'{game.name}-finish-{track}.log')

	return count


def check_live(build, game, cwd, reports):
	client = game / 'openmotor'
	shutil.copy2(build_executable(build, 'openmotor'), client)
	log = reports / (game.name + '-live.log')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as stream:
		process = subprocess.Popen([str(client), '--debug-no-intro', '--debug-skip-loading-wait', '1',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1'], cwd=cwd, env=env,
			stdout=stream, stderr=stream)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)
			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 8)
			shots = game / 'userdata'
			key(race, 'F12', process, log)
			wait(process, log, lambda: shots.is_dir() and any(p.suffix.lower() in ('.png', '.tga') for p in shots.iterdir()))

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			stop(process)

	settings = game / 'userdata/motorhead.cfg'
	assert settings.is_file(), 'settings were not saved in the game directory'
	saved = settings.read_bytes()

	# Relaunch from inside the installation with its saved settings.
	capture = reports / (game.name + '-relaunch.bmp')
	capture.unlink(missing_ok=True)
	result = subprocess.run([str(client), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-frames', '3',
		'--debug-capture', str(capture),
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1'], cwd=game, env=env,
		capture_output=True, text=True, timeout=90)
	(reports / (game.name + '-relaunch.log')).write_text(result.stdout + result.stderr)
	assert result.returncode == 0 and capture.read_bytes()[:2] == b'BM', result.stderr
	assert settings.read_bytes() == saved, 'relaunch unexpectedly changed settings'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--demos', type=Path, required=True,
		help='directory containing one unmodified installation per demo')
	parser.add_argument('--prepared', type=Path, required=True)
	parser.add_argument('--reports', type=Path, default=Path('build/checks/compatibility/demo-dropin'))
	parser.add_argument('--live', action='store_true')
	args = parser.parse_args()
	build, reports = args.build.resolve(), args.reports.resolve()
	reports.mkdir(parents=True, exist_ok=True)
	results = []

	with tempfile.TemporaryDirectory(prefix='original-demos-', dir=reports) as temporary:
		cwd = Path(temporary)

		for source in sorted(args.demos.iterdir()):
			if not source.is_dir():
				continue

			game = cwd / source.name
			shutil.copytree(source, game)
			before = originals(game)
			count = check_assets(build, game, args.prepared / game.name, reports)

			if args.live:
				check_live(build, game, cwd, reports)

			assert originals(game) == before, 'original installation was changed'
			results.append(dict(edition=game.name, race_cases=count, live=args.live))
			print(f'{game.name}: providers, {count} car/course/profile combinations, '
				f'finish/restart and original hashes passed; live={args.live}', flush=True)

	assert len(results) == 7, 'expected all seven supplied demos'
	(reports / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
	main()
