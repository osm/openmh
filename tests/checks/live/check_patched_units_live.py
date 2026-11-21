#!/usr/bin/env python3
"""Check patched System persistence and both race HUDs under X11.

Uses isolated settings and deterministic replay ticks. Original arithmetic and
key-event behavior are covered separately by the unit tests.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.support.application import frontend
from tests.checks.support.images import bmp_rgb
from tests.checks.support.menu import menu_key, diagnostic, wait, window_for
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def run(args, root):
	cfg = root/'test.cfg'
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	values = dict(System=b'Metric', DetailMode=b'Custom', ViewDistance=b'80',
		ScreenDimension=b'640 400', InfoDetail=b'All',
		SFXVolume=b'0', MusicVolume=b'0', MenuConfirmation=b'Off', RecordRace=b'Off')
	write_cfg(cfg, values)
	menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--debug-page', 'personal', '--debug-selection', '3',
		'--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0', '--debug-no-records', '--debug-camera-trace', '1', '--debug-window-titles', '1']

	for button, expected in [('Right', b'Imperial'), ('Left', b'Metric')]:
		log = root/'menu.log'

		with frontend(menu, [], log, env) as (process, window):
			menu_key(window, button)

			if expected == b'Imperial':
				for action in ('Escape', 'Escape', 'Return', 'Return'):
					menu_key(window, action)

				race = window_for(process, log, r' \| [0-9]+ mph \| ')
				xdo('windowfocus', '--sync', race)
				wait_frames(process, log, frames(log), 6)
				assert read_cfg(cfg)[b'System'] == expected

				for action in ('Escape', 'Up'):
					key(race, action, process, log)

				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
				finally:
					xdo('keyup', 'Return')

		assert read_cfg(cfg)[b'System'] == expected, diagnostic(log)

	print('Personal System: real input, same-process race/return, save and application restart passed', flush=True)
	replay = root/'drive.txt'
	replay.write_text('1 0 0 0 0 0 0\n'*100)

	for renderer in ('software', 'glide'):
		traces, captures, menus = [], [], []

		for system in (b'Metric', b'Imperial'):
			name = f'{renderer}-{system.decode()}'
			values['System'] = system
			write_cfg(cfg, values)
			saved = cfg.read_bytes()
			capture = root/(name+'.bmp')
			trace = root/(name+'.trace')
			command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
				'--renderer', renderer, '--debug-settings', str(cfg),
				'--debug-car', '1', '--debug-track', '1', '--debug-no-countdown',
				'--debug-replay', str(replay), '--debug-trace', str(trace),
				'--debug-capture', str(capture), '--debug-mute', '--debug-no-records',
				'--debug-camera-trace', '1',
				'--debug-window-titles', '1']
			result = subprocess.run(command, env=env, capture_output=True, timeout=90)
			assert result.returncode == 0, result.stderr.decode(errors='replace')
			assert cfg.read_bytes() == saved
			traces.append(trace.read_bytes())
			captures.append(bmp_rgb(capture))
			menu_capture = root/(name+'-menu.bmp')
			result = subprocess.run(menu+['--renderer', renderer, '--debug-frames', '30',
				'--debug-capture', str(menu_capture)],
				env=env, capture_output=True, timeout=60)
			assert result.returncode == 0, result.stderr.decode(errors='replace')
			menus.append(bmp_rgb(menu_capture))

		assert traces[0] == traces[1], 'System affected simulation trace'
		changed = [i//3 for i, (a, b) in enumerate(zip(*captures)) if a != b]
		assert changed, 'unit choice did not change rendered HUD'
		assert all(p % 640 < 160 and p // 640 < 80 for p in changed), 'units changed pixels outside gauges'
		assert menus[0] != menus[1], 'System menu choice not visible'
		print(f'{renderer}: metric/imperial HUD and menu captures differ; physics traces match; captures preserve CFG', flush=True)

	# Old artwork still supports metric; do not silently label MPH as KM/H.
	for renderer in ('software', 'glide'):
		values['System'] = b'Metric'
		write_cfg(cfg, values)
		command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.legacy),
			'--renderer', renderer, '--debug-settings', str(cfg), '--debug-car', '1',
			'--debug-track', '1', '--debug-mute', '--debug-no-records', '--debug-capture', str(root/'legacy.bmp'),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		result = subprocess.run(command, env=env, capture_output=True, timeout=90)
		assert result.returncode == 0, result.stderr.decode(errors='replace')

	print('Both renderers still load unpatched metric artwork', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--legacy', type=Path, required=True)
	parser.add_argument('--capture-dir', type=Path)
	args = parser.parse_args()

	if args.capture_dir:
		args.capture_dir.mkdir(parents=True, exist_ok=True)
		run(args, args.capture_dir)
	else:
		with tempfile.TemporaryDirectory(prefix='mh-patched-units-live-') as directory:
			run(args, Path(directory))


if __name__ == '__main__':
	main()
