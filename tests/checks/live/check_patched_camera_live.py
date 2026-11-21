#!/usr/bin/env python3
"""Exercise saved camera views through X11 races and frontend return."""

from tests.checks.support.build import build_executable
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def mode(log):
	rows = [line.split() for line in log.read_text().splitlines()
		if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:')]

	return int(rows[-1][5]) if rows else None


def resume(process, window, log):
	wait_frames(process, log, frames(log), 6)
	rows = [line.split() for line in log.read_text().splitlines()
		if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:')]

	if rows[-1][6] == '1':
		key(window, 'Escape', process, log)
		key(window, 'Escape', process, log)


def settle(process, log, seconds=12):
	deadline = time.monotonic()+seconds
	wait(process, log, lambda: time.monotonic() >= deadline)


@contextmanager
def race(command, log, env):
	with log.open('w') as stream:
		process = subprocess.Popen(command, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, r'\| 1 cars \|')
			xdo('windowfocus', '--sync', window)
			resume(process, window, log)
			yield process, window
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


def run(args, root):
	cfg, log = root/'camera.cfg', root/'race.log'
	values = dict(PlayerName='Camera', TeamName='NOW', GameType='Single',
		CarName=fields(args.gamedir/'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'],
		RecordRace='Off', MenuConfirmation='Off', DetailMode='Low',
		ScreenDimension='640 400', SFXVolume='0', MusicVolume='0',
		CarView='Outside')
	cfg.write_bytes(encode(values))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--renderer', args.renderer,
		'--debug-car', '1', '--debug-track', '1', '--debug-opponents', '0',
		'--debug-no-records', '--debug-mute', '--debug-camera-trace', '1', '--debug-window-titles', '1']

	with race(command, log, env) as (process, window):
		settle(process, log)
		assert mode(log) == 0, diagnostic(log)
		key(window, 'F1', process, log)
		assert mode(log) == 1, diagnostic(log)
		key(window, 'F3', process, log)
		assert mode(log) == 2, diagnostic(log)

	assert read_cfg(cfg)['CarView'] == 'Inside', read_cfg(cfg)
	print('Outside restores after intro; F1 then F3 saves Inside', flush=True)

	with race(command, log, env) as (process, window):
		settle(process, log)
		assert mode(log) == 1, diagnostic(log)
		key(window, 'F2', process, log)
		key(window, 'F3', process, log)
		assert mode(log) == 2, diagnostic(log)

		for button in ('Escape', 'Down', 'Return'):
			key(window, button, process, log)

		settle(process, log)
		assert mode(log) == 0, diagnostic(log)

	assert read_cfg(cfg)['CarView'] == 'Outside', read_cfg(cfg)
	print('Relaunch restores Inside; F2/F3 and restart retain Outside', flush=True)

	menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--renderer', args.renderer,
		'--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-no-records', '--audio', '0', '--debug-camera-trace', '1', '--debug-window-titles', '1']

	with frontend(menu, [], log, env) as (process, window):
		menu_key(window, 'Return')
		menu_key(window, 'Return')
		window = window_for(process, log, r'\| 8 cars \|')
		xdo('windowfocus', '--sync', window)
		resume(process, window, log)
		settle(process, log)
		assert mode(log) == 0, diagnostic(log)
		key(window, 'F1', process, log)
		assert mode(log) == 1, diagnostic(log)

		for button in ('Escape', 'Up'):
			key(window, button, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
		finally:
			xdo('keyup', 'Return')

	assert read_cfg(cfg)['CarView'] == 'Inside', read_cfg(cfg)
	before = cfg.read_bytes()
	result = subprocess.run(command+['--debug-capture', str(root/'race.bmp'), '--debug-ticks', '2'],
		env=env, capture_output=True, timeout=60)
	assert result.returncode == 0 and (root/'race.bmp').is_file(), result.stderr
	assert cfg.read_bytes() == before
	print(f'{args.renderer}: frontend race return saves Inside; captures leave CFG untouched', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('software', 'glide'), default='software')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-patched-camera-live-') as directory:
		run(args, Path(directory))


if __name__ == '__main__':
	main()
