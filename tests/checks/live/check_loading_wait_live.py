#!/usr/bin/env python3
"""Verify the local loading prompt, fresh input, stopped race clock and close handling."""

import argparse
import os
from pathlib import Path
import tempfile
import time

from tests.checks.support.application import frontend
from tests.checks.support.build import build_executable
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import frames, wait_frames, xdo
from tests.checks.live.check_menu_credits_live import capture


def assert_waiting(process, log, seconds=1):
	time.sleep(seconds)
	assert process.poll() is None, diagnostic(log)
	assert frames(log) == 0 and 'Race renderer:' not in log.read_text(), diagnostic(log)


def run_race(command, env, root, renderer, captures):
	log = root / f'{renderer}.log'

	with frontend(command, ['--renderer', renderer, '--debug-page', 'car'], log, env) as (process, menu):
		# Keep the menu's Enter held throughout loading, including key repeats.
		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: 'Loading ready:' in log.read_text())
			loading = window_for(process, log, '^Motorhead loading$')
			assert_waiting(process, log)
		finally:
			xdo('keyup', 'Return')

		assert_waiting(process, log, .3)

		if captures:
			captures.mkdir(parents=True, exist_ok=True)
			capture(loading, captures / f'{renderer}.png')

		# The window remains responsive; resizing does not dismiss the prompt.
		xdo('windowsize', loading, '800', '500')
		assert_waiting(process, log, .3)
		menu_key(loading, 'space')
		window_for(process, log, r'TIME ATTACK \| 1 cars')
		wait_frames(process, log, frames(log), 5)
		first = next(line.split() for line in log.read_text().splitlines()
			if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:'))
		assert float(first[1]) < .5 and int(first[4]) < 60, first
		assert 'Pause:' not in log.read_text(), diagnostic(log)

	print(f'{renderer}: fresh key starts the race; waiting does not advance physics', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--assets', type=Path, required=True)
	parser.add_argument('--captures', type=Path)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-loading-wait-') as temporary:
		root = Path(temporary)
		(root / 'motorhead.cfg').write_text('ScreenDimension 640 400\n'
			'MenuConfirmation Off\nRecordRace Off\nSFXVolume 0\nMusicVolume 0\n')
		command = [str(build_executable(args.build, 'openmotor')), '--assets', str(args.assets.resolve()),
			'--user-data', str(root), '--debug-no-intro', '--debug-no-records', '--audio', '0',
			'--debug-car', '1', '--debug-track', '1', '--debug-mode', '2',
			'--debug-record', '0', '--debug-camera-trace', '1', '--debug-window-titles', '1']
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		for renderer in ('software', 'glide', 'direct3d', 'powervr'):
			run_race(command, env, root, renderer, args.captures)

		# Close directly while the loading view still owns input and rendering.
		log = root / 'close.log'

		with frontend(command, ['--renderer', 'software', '--debug-page', 'car'], log, env) as (process, menu):
			menu_key(menu, 'Return')
			wait(process, log, lambda: 'Loading ready:' in log.read_text())
			assert_waiting(process, log, .3)

		assert frames(log) == 0
		print('Closing the ready screen exits without starting a race', flush=True)
		log = root / 'bypass.log'

		with frontend(command, ['--renderer', 'software', '--debug-page', 'car',
			'--debug-skip-loading-wait', '1'], log, env) as (process, menu):
			menu_key(menu, 'Return')
			window_for(process, log, r'TIME ATTACK \| 1 cars')
			wait_frames(process, log, frames(log), 5)
			assert 'Loading ready:' not in log.read_text()

		print('Diagnostic bypass starts immediately', flush=True)


if __name__ == '__main__':
	main()
