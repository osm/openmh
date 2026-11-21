#!/usr/bin/env python3
"""Exercise saved Direct3D history selection, live races, resizing and isolation."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.images import bmp_rgb
from tests.checks.live.check_presentation_live import race


def headless(args):
	env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy')

	with tempfile.TemporaryDirectory(prefix='mh-direct3d-blur-capture-') as directory:
		root = Path(directory)
		cfg = root / 'settings.cfg'

		for setting, history in (('Off', 2), ('On', 3)):
			content = encode(dict(Renderer='Direct3D', ScreenDimension='640 400',
				DetailMode='Custom', MotionBlur='On', TrippleBuffer=setting,
				FrameQueue='1', CarShading='Glenz', TextureFiltering='Trilinear'))
			cfg.write_bytes(content)
			capture = root / f'{setting}.bmp'
			result = subprocess.run([str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
				'--debug-settings', str(cfg), '--renderer', 'direct3d', '--debug-car', '1', '--debug-track', '1',
				'--debug-opponents', '7', '--debug-ticks', '120', '--debug-ai', '1', '--debug-no-countdown',
				'--debug-no-records', '--debug-mute', '--debug-headless', str(capture), '--debug-window-titles', '1'], env=env,
				capture_output=True, text=True, timeout=120)
			assert result.returncode == 0, result.stdout+result.stderr
			assert 'Race renderer: Direct3D' in result.stdout
			assert f'Race blur history: {history}' in result.stderr
			assert 'Race frame queue:' not in result.stderr
			assert cfg.read_bytes() == content
			pixels = bmp_rgb(capture)
			assert len(pixels) == 640*400*3 and len(set(pixels)) > 32
			print(f'Headless Direct3D: {history}-target eight-car race/capture/cleanup and immutable CFG pass', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--headless-only', action='store_true', help='verify race startup and cleanup without X11')
	parser.add_argument('--capture', type=Path, help='retain the selected menu row')
	args = parser.parse_args()

	if args.headless_only:
		headless(args)

		return

	with tempfile.TemporaryDirectory(prefix='mh-direct3d-blur-live-') as directory:
		root = Path(directory)
		cfg, log = root / 'settings.cfg', root / 'live.log'
		cfg.write_bytes(encode(dict(Renderer='Direct3D', ScreenDimension='640 400',
			DetailMode='Custom', MotionBlur='On', TrippleBuffer='Off', FrameQueue='1',
			SFXVolume='0', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		common = ['--assets', str(args.gamedir)]
		menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-car', '1', '--debug-track', '1', '--debug-opponents', '7', '--debug-mute',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		for key, setting, history in (('Right', 'On', 3), ('Left', 'Off', 2), ('Right', 'On', 3)):
			with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', '25'], log, env) as (_, window):
				menu_key(window, 'Down')
				menu_key(window, key)

			assert 'focus 26' in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['TrippleBuffer'] == setting
			assert read_cfg(cfg)['FrameQueue'] == '1'

			# No CLI profile override: reload the saved renderer and preference.
			race(drive, env, log, '1')
			assert 'Race renderer: Direct3D' in log.read_text(), diagnostic(log)
			assert f'Race blur history: {history}' in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['TrippleBuffer'] == setting
			print(f'Direct3D TrippleBuffer {setting}: menu/save/reload, {history}-target race and resize pass', flush=True)

		if args.capture is not None:
			before = cfg.read_bytes()
			subprocess.run(menu + ['--debug-page', 'graphics', '--debug-selection', '26', '--debug-frames', '30',
				'--debug-capture', str(args.capture.resolve())], env=env, check=True,
				capture_output=True, timeout=90)
			assert cfg.read_bytes() == before

		for profile, saved, start, queue in (('glide', 'Glide', 25, '1'),
			('software', 'Software', 22, None)):
			race(drive + ['--renderer', profile], env, log, queue)
			assert 'Race blur history:' not in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['TrippleBuffer'] == 'On'
			assert read_cfg(cfg)['Renderer'] == saved

			with frontend(menu, ['--debug-page', 'graphics', '--debug-selection', str(start)], log, env) as (_, window):
				menu_key(window, 'Down')

			assert 'focus 26' not in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['TrippleBuffer'] == 'On'

		print('Glide/Software hide the option, preserve it and keep their existing history policy', flush=True)


if __name__ == '__main__':
	main()
