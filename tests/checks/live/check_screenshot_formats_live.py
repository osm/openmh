#!/usr/bin/env python3
"""Check screenshot menu persistence and F12 PNG/TGA captures on X11 (FFmpeg required)."""

import argparse
import os
from pathlib import Path
import tempfile
import subprocess
import json


from tests.checks.support.application import frontend
from tests.checks.support.build import build_executable
from tests.checks.support.menu import menu_key, wait, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def select_format(command, env, root, direction, expected):
	log = root / 'graphics.log'

	with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '30'],
		log, env) as (_, menu):
		menu_key(menu, direction)

	assert read_cfg(root / 'motorhead.cfg')['ScreenshotFormat'] == expected


def capture_race(command, env, root, renderer, extension):
	log = root / f'{renderer}-{extension}.log'
	shots = root
	arguments = ['--renderer', renderer, '--debug-page', 'car',
		'--debug-car', '1', '--debug-track', '1', '--debug-mode', '2',
		'--debug-record', '0', '--debug-camera-trace', '1']

	with frontend(command, arguments, log, env) as (process, menu):
		menu_key(menu, 'Return')
		race = window_for(process, log, r'TIME ATTACK \| 1 cars')
		xdo('windowfocus', '--sync', race)
		wait_frames(process, log, frames(log), 6)
		previous = {p.name: p.read_bytes() for p in shots.iterdir()
			if p.suffix in ('.png', '.tga')}
		key(race, 'F12', process, log)
		wait(process, log, lambda: len([p for p in shots.iterdir()
			if p.suffix in ('.png', '.tga')]) == len(previous) + 1)
		new = [p for p in shots.iterdir()
			if p.suffix in ('.png', '.tga') and p.name not in previous]
		assert len(new) == 1 and new[0].suffix == '.' + extension, new
		metadata = subprocess.run(['ffprobe', '-v', 'error', '-show_streams',
			'-of', 'json', str(new[0])], capture_output=True, text=True, check=True, timeout=15)
		stream = json.loads(metadata.stdout)['streams'][0]
		assert (stream['width'], stream['height']) == (640, 400), stream
		assert stream['codec_name'] == ('png' if extension == 'png' else 'targa'), stream
		decoded = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(new[0]),
			'-f', 'rawvideo', '-pix_fmt', 'rgba', '-'], capture_output=True, check=True, timeout=15)
		assert len(decoded.stdout) == 640 * 400 * 4
		assert len(set(decoded.stdout)) > 1, 'blank screenshot'
		assert set(decoded.stdout[3::4]) == {255}, 'screenshot contains transparent pixels'

		for name, data in previous.items():
			assert (shots / name).read_bytes() == data, name

	assert read_cfg(root / 'motorhead.cfg')['ScreenshotFormat'] == extension.upper()
	print(f'{renderer}: {extension.upper()} capture decoded; earlier files preserved', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--assets', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-screenshot-formats-') as temporary:
		root = Path(temporary)

		# Omit ScreenshotFormat to exercise the default encoder.
		(root / 'motorhead.cfg').write_text('ScreenDimension 640 400\n'
			'MenuConfirmation Off\nRecordRace Off\nSFXVolume 0\nMusicVolume 0\n')
		command = [str(build_executable(args.build, 'openmotor')),
			'--assets', str(args.assets.resolve()), '--user-data', str(root),
			'--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-no-records', '--audio', '0']
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		for renderer in ('software', 'glide', 'direct3d', 'powervr'):
			capture_race(command, env, root, renderer, 'png')

		select_format(command, env, root, 'Right', 'TGA')
		capture_race(command, env, root, 'software', 'tga')
		capture_race(command, env, root, 'glide', 'tga')
		select_format(command, env, root, 'Left', 'PNG')
		capture_race(command, env, root, 'software', 'png')
		print('PNG default, menu selection, save/reload and F12 numbering passed', flush=True)


if __name__ == '__main__':
	main()
