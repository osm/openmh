#!/usr/bin/env python3
"""Verify race/menu keyboard focus without refocusing the returned window.

Run on an isolated X server with a window manager. The ordinary menu helpers
explicitly set focus, so this check sends bare key events after the handoff.
"""

import argparse
import os
from pathlib import Path
import tempfile
import time

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
from tests.checks.live.check_fullscreen_live import dimensions
from tests.checks.support.application import frontend
from tests.checks.live.check_opening_window_live import WindowWatch
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def expect_focus(process, log, window):
	wait(process, log, lambda: int(xdo('getwindowfocus').strip()) == window,
		seconds=10)

	# Catch a delayed focus loss after pending window-manager events settle.
	time.sleep(.3)
	assert int(xdo('getwindowfocus').strip()) == window, diagnostic(log)


def run(binary, game, root, renderer, fullscreen, resolution=(640, 400), verbose=True):
	cfg, log = root / 'settings.cfg', root / 'live.log'
	cfg.write_text(f'ScreenDimension {resolution[0]} {resolution[1]}\n'
		'DetailMode Custom\nMenuConfirmation Off\n'
		'RecordRace Off\nSFXVolume 0\nMusicVolume 0\n')
	command = [str(binary), '--assets', str(game), '--debug-settings', str(cfg),
		'--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0', '--debug-no-records', '--debug-page', 'car',
		'--renderer', renderer, '--debug-car', '1', '--debug-track', '1',
		'--fullscreen', '1' if fullscreen else '0',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1' if verbose else '0']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	if not verbose:
		command += ['--debug-menu-clock', '1']

	with frontend(command, [], log, env,
		title='main-menu preview' if verbose else '^Motorhead$',
		ready=None if verbose else 'menu-clock ') as (process, menu):
		if not fullscreen:
			xdo('windowsize', '--sync', menu, 734, 492)

		menu_size = dimensions(menu)
		watch = WindowWatch(menu)

		try:
			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| 8 cars \|' if verbose else r'^Motorhead - ')
			assert race == menu, 'race replaced the client window'

			if not verbose:
				assert xdo('getwindowname', race).strip() == 'Motorhead - RedRock'

			expected_size = menu_size if fullscreen else resolution
			assert dimensions(race) == expected_size, (dimensions(race), expected_size)
			assert f'Race viewport: {resolution[0]}x{resolution[1]}' in log.read_text(), diagnostic(log)
			watch.check()
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			expect_focus(process, log, menu)
			assert dimensions(menu) == menu_size, (dimensions(menu), menu_size)

			# No windowfocus/windowactivate or --window argument here.
			xdo('keydown', 'Right')
			time.sleep(.1)
			xdo('keyup', 'Right')
			time.sleep(.3)
			watch.check()

			if not verbose:
				assert xdo('getwindowname', menu).strip() == 'Motorhead'
		finally:
			watch.close()

	assert 'Menu preview: selection 1,' in log.read_text(), diagnostic(log)
	print(f'{renderer}/{"fullscreen" if fullscreen else "windowed"}: '
		'one mapped window, correct race/menu dimensions and keyboard focus', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', action='append',
		choices=('software', 'glide', 'direct3d', 'powervr'))
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-window-focus-') as temporary:
		root = Path(temporary)
		game = root / 'installation'
		prepare_view(game, args.gamedir.resolve())

		for renderer in args.renderer or ('software', 'glide', 'direct3d', 'powervr'):
			for fullscreen in (False, True):
				run(build_executable(args.build.resolve(), 'openmotor'), game, root,
					renderer, fullscreen)

		for fullscreen in (False, True):
			run(build_executable(args.build.resolve(), 'openmotor'), game, root,
				'glide', fullscreen, (800, 600), verbose=False)


if __name__ == '__main__':
	main()
