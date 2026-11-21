#!/usr/bin/env python3
"""Exercise saved Glenz selection and pause-map lifetime through X11 input.

Run under xvfb-run. Uses patched assets, dummy audio and temporary CFG files.
Every child is reaped, including when an assertion fails.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import close_window, frames, key, wait_frames


def race(command, cfg, env, log, shading, edits, expected):
	cfg.write_text('DetailMode Custom\nScreenDimension 320 240\n'
		f'CarShading {shading}\nTexturePrecision 16\nSFXVolume 0\nMusicVolume 0\n')

	with log.open('w') as stream:
		process = subprocess.Popen(command, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, r'\| 8 cars \|')
			wait_frames(process, log, frames(log), 6)

			if shading == 'Glenz':
				for name in ('r', 'Home'):
					key(window, name, process, log)
					wait_frames(process, log, frames(log), 6)

			for name in ('Escape', 'Down', 'Down', 'Return', *edits):
				key(window, name, process, log)

			close_window(int(window))
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	assert read_cfg(cfg)['CarShading'] == expected, read_cfg(cfg)
	assert read_cfg(cfg)['TexturePrecision'] == '16', read_cfg(cfg)
	assert log.read_text().count('Glenz materials loaded') == (shading == 'Glenz')

	if shading == 'Glenz':
		assert 'Glenz materials loaded (16-bit maps)' in log.read_text(), diagnostic(log)

	assert 'Reset:' not in log.read_text(), diagnostic(log)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-glenz-settings-live-') as tmp:
		root = Path(tmp)
		cfg, log = root / 'settings.cfg', root / 'live.log'
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		common = ['--assets', str(args.gamedir)]
		menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		cfg.write_text('DetailMode Custom\nScreenDimension 640 400\n'
			'CarShading Reflection\nRenderer Glide\n')

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '18'], log, env) as (_, window):
			menu_key(window, 'Right')

		assert read_cfg(cfg)['CarShading'] == 'Glenz', read_cfg(cfg)
		assert read_cfg(cfg)['TexturePrecision'] == '32', read_cfg(cfg)

		for name, precision in (('Left', '16'), ('Right', '32'), ('Left', '16')):
			with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '22'], log, env) as (_, window):
				menu_key(window, 'Down')
				menu_key(window, name)

			assert read_cfg(cfg)['TexturePrecision'] == precision, read_cfg(cfg)
			assert 'focus 23' in log.read_text(), diagnostic(log)

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '0'], log, env) as (_, window):
			menu_key(window, 'Left')

		assert read_cfg(cfg)['CarShading'] == 'Reflection', read_cfg(cfg)
		assert read_cfg(cfg)['Renderer'] == 'Software', read_cfg(cfg)
		assert read_cfg(cfg)['TexturePrecision'] == '16', read_cfg(cfg)

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '22'], log, env) as (_, window):
			menu_key(window, 'Down')

		assert 'focus 23' not in log.read_text(), diagnostic(log)
		print('Glenz: shading/precision save and reload; software hides precision and retains its preference', flush=True)

		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--renderer', 'glide',
			'--debug-car', '1', '--debug-track', '1', '--debug-opponents', '7',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		race(drive, cfg, env, log, 'Glenz', ('Left', 'Right'), 'Glenz')
		race(drive, cfg, env, log, 'Glenz', ('Left',), 'Reflection')
		race(drive, cfg, env, log, 'Reflection', ('Right',), 'Reflection')
		print('Glenz: live recovery/restart, pause off/on, saved return, and unavailable-map gate passed', flush=True)


if __name__ == '__main__':
	main()
