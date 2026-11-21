#!/usr/bin/env python3
"""Verify GPU frame queue settings, menu scrolling and software isolation on X11."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, menu_text, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import close_window, frames, wait_frames, xdo


def race(command, env, log, expected, vsync="On", max_fps=0):
	with log.open('w') as stream:
		process = subprocess.Popen(command, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, r'\| 8 cars \|')
			wait_frames(process, log, frames(log), 6)
			xdo('windowsize', window, '800', '600')
			wait_frames(process, log, frames(log), 6)

			if max_fps:
				before, started = frames(log), time.monotonic()
				time.sleep(1.0)
				elapsed = time.monotonic() - started
				count = frames(log) - before
				assert count <= max_fps * elapsed + 2, (count, elapsed, max_fps)

			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	assert f'Race VSync: {vsync}' in log.read_text(), diagnostic(log)
	lines = [line for line in log.read_text().splitlines() if line.startswith('Race frame queue:')]
	assert lines == ([] if expected is None else [f'Race frame queue: {expected}']), diagnostic(log)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-presentation-live-') as tmp:
		root = Path(tmp)
		cfg, log = root / 'settings.cfg', root / 'live.log'
		cfg.write_text('DetailMode Custom\nScreenDimension 640 400\nRenderer Glide\n'
			'CarShading Reflection\nSFXVolume 0\nMusicVolume 0\n')
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		common = ['--assets', str(args.gamedir)]
		menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-car', '1', '--debug-track', '1', '--debug-opponents', '7',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		for key, expected in (('Left', '1'), ('Right', '2'), ('Right', '3')):
			with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '24'], log, env) as (_, window):
				menu_key(window, 'Down')
				menu_key(window, key)

			assert 'focus 25' in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['FrameQueue'] == expected, read_cfg(cfg)
			assert read_cfg(cfg)['VSync'] == 'On', read_cfg(cfg)
			race(drive + ['--renderer', 'glide'], env, log, expected)
			assert read_cfg(cfg)['FrameQueue'] == expected

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '28'], log, env) as (_, window):
			menu_key(window, 'Right')
			menu_key(window, 'Down')
			menu_text(window, '237')
			menu_key(window, 'Return')
			menu_text(window, '999')
			menu_key(window, 'Escape')

		assert read_cfg(cfg)['VSync'] == 'On', read_cfg(cfg)
		assert read_cfg(cfg)['MaxFPS'] == '237', read_cfg(cfg)
		race(drive + ['--renderer', 'glide'], env, log, '3', vsync='On', max_fps=237)

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '28'], log, env) as (_, window):
			menu_key(window, 'Left')

		assert read_cfg(cfg)['VSync'] == 'Off', read_cfg(cfg)

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '0'], log, env) as (_, window):
			menu_key(window, 'Left')

		assert read_cfg(cfg)['Renderer'] == 'Software'

		with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '22'], log, env) as (_, window):
			menu_key(window, 'Down')

		assert 'focus 25' not in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['FrameQueue'] == '3'
		race(drive + ['--renderer', 'software'], env, log, None, vsync='Off', max_fps=237)
		assert read_cfg(cfg)['MaxFPS'] == '237'
		assert read_cfg(cfg)['FrameQueue'] == '3'
		print('Presentation: VSync/FPS cap save/reload, 1/2/3 queues, scrolled menu, GPU race startup/resizing and software isolation passed')


if __name__ == '__main__':
	main()
