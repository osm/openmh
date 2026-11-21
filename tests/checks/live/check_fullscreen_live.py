#!/usr/bin/env python3
"""Check fullscreen preference, movies and all four race renderers under X11.

Run on an isolated X server with an EWMH window manager; bare Xvfb cannot
honor fullscreen requests. Source assets are borrowed through a temporary view.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def dimensions(window):
	values = dict(line.split('=', 1) for line in
		xdo('getwindowgeometry', '--shell', window).splitlines())

	return int(values['WIDTH']), int(values['HEIGHT'])


def expect_size(process, log, window, size):
	try:
		wait(process, log, lambda: dimensions(window) == size, seconds=10)
	except AssertionError:
		raise AssertionError(f'window {window}: expected {size}, got {dimensions(window)}; {diagnostic(log)}')


def stop(process):
	if process.poll() is None:
		process.terminate()

		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
			process.wait(timeout=5)


def check_movie(command, env, log, screen):
	with log.open('w') as stream:
		process = subprocess.Popen(command, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, 'Motorhead movie')
			expect_size(process, log, window, screen)
			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			stop(process)


def check_race(command, env, log, root, screen, renderer, captures):
	with frontend(command, ['--renderer', renderer, '--fullscreen', '1', '--debug-no-intro', '--debug-skip-loading-wait', '1',
		'--debug-page', 'car', '--show-fps', '1'], log, env) as (process, menu):
		expect_size(process, log, menu, screen)
		menu_key(menu, 'Return')
		race = window_for(process, log, r'\| 8 cars \|')
		expect_size(process, log, race, screen)
		xdo('windowfocus', '--sync', race)
		wait_frames(process, log, frames(log), 3)
		time.sleep(1.2)  # Let the half-second FPS sample settle.
		key(race, 'F12', process, log)
		shots = root / 'userdata'
		wait(process, log, lambda: bool(list(shots.glob('*.png'))))
		shot = next(shots.glob('*.png'))
		data = shot.read_bytes()
		assert data.startswith(b'\x89PNG\r\n\x1a\n'), 'missing PNG race capture'

		if captures is not None:
			captures.mkdir(parents=True, exist_ok=True)
			(captures / f'{renderer}.png').write_bytes(data)

		for path in shots.glob('*.png'):
			path.unlink()

		# Abort the race and ensure the original fullscreen menu is restored.
		for name in ('Escape', 'Up'):
			key(race, name, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
		finally:
			xdo('keyup', 'Return')

		expect_size(process, log, menu, screen)

	print(f'{renderer}: fullscreen loading/race, F12 capture and menu return passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--captures', type=Path)
	args = parser.parse_args()
	screen = tuple(map(int, xdo('getdisplaygeometry').split()))

	with tempfile.TemporaryDirectory(prefix='mh-fullscreen-') as temporary:
		root = Path(temporary)
		game = root / 'installation'
		prepare_view(game, args.gamedir.resolve())
		cfg, log = root / 'settings.cfg', root / 'live.log'
		cfg.write_text('Fullscreen Off\nScreenDimension 640 400\n'
			'SFXVolume 0\nMusicVolume 0\nMenuConfirmation Off\n'
			'RecordRace Off\nDetailMode High\n')
		command = [str(build_executable(args.build.resolve(), 'openmotor')), '--user-data', str(root / 'userdata'),
			'--assets', str(game), '--debug-settings', str(cfg),
			'--debug-no-records', '--audio', '0', '--debug-car', '1', '--debug-track', '1',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		with frontend(command, ['--fullscreen', '1', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-page', 'graphics',
			'--debug-selection', '27'], log, env) as (process, window):
			expect_size(process, log, window, screen)
			menu_key(window, 'Left')
			expect_size(process, log, window, (960, 600))
			xdo('windowsize', window, '800', '500')
			expect_size(process, log, window, (800, 500))
			menu_key(window, 'Right')
			expect_size(process, log, window, screen)
			menu_key(window, 'Left')
			expect_size(process, log, window, (800, 500))
			menu_key(window, 'Right')

		assert read_cfg(cfg)['Fullscreen'] == 'On'

		# Saved preference applies even before the main menu opens.
		check_movie(command, env, log, screen)

		with frontend(command, ['--fullscreen', '0', '--debug-no-intro', '--debug-skip-loading-wait', '1'], log, env) as (process, window):
			expect_size(process, log, window, (960, 600))

		assert read_cfg(cfg)['Fullscreen'] == 'Off'
		print('Fullscreen menu toggle, resize restoration, persistence, intro and CLI overrides passed', flush=True)

		for renderer in ('software', 'glide', 'direct3d', 'powervr'):
			check_race(command, env, log, root, screen, renderer, args.captures)


if __name__ == '__main__':
	main()
