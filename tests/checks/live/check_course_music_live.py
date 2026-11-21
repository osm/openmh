#!/usr/bin/env python3
"""Verify patched per-course music edits, restart and race handoff under X11.

Audio uses the dummy device and extracted CD WAVs. Settings, logs and captures
are private; original assets are never rewritten.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def run(args, root):
	cfg, log = root/'music.cfg', root/'menu.log'
	values = dict(PlayerName='Music', TeamName='NOW', GameType='Single',
		CarName=fields(args.gamedir/'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'],
		RecordRace='Off', MenuConfirmation='Off', DetailMode='Low',
		ScreenDimension='640 400', SFXVolume='0', MusicVolume='20', CDLoop='Off')
	cfg.write_bytes(encode(values))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--renderer', args.renderer,
		'--debug-recordings', str(root/'recordings'),
		'--debug-page', 'sound', '--debug-selection', '3', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-no-records', '--debug-require-audio',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']

	with frontend(command, [], log, env) as (process, window):
		# Division order: Goldbridge, RedRock, GoldbridgeR, RedRockR.
		for button in ('Down', 'Right', 'Return'):
			menu_key(window, button)

		wait(process, log, lambda: 'Frontend music: CD track 5' in log.read_text())

		for button in ('Down', 'Down', 'Right', 'Right', 'Return'):
			menu_key(window, button)

		wait(process, log, lambda: 'Frontend music: CD track 6' in log.read_text())

	saved = read_cfg(cfg)
	assert saved['CDTrack_101'] == '5' and saved['CDTrack_111'] == '6', saved
	assert saved['MenuCDTrack'] == '3' and saved['CDTrack_100'] == '11', saved
	assert len([k for k in saved if k.startswith('CDTrack_')]) == 16
	print('Sound: forward/reverse edits audition correctly and save all 16 Division keys', flush=True)

	with frontend(command, [], log, env) as (process, window):
		for button in ('Down', 'Return'):
			menu_key(window, button)

		wait(process, log, lambda: 'Frontend music: CD track 5' in log.read_text())

		for button in ('Down', 'Down', 'Return'):
			menu_key(window, button)

		wait(process, log, lambda: 'Frontend music: CD track 6' in log.read_text())

		for button in ('Escape', 'Escape', 'Return', 'Return'):
			menu_key(window, button)

		race = window_for(process, log, r'\| 8 cars \|')
		xdo('windowfocus', '--sync', race)
		wait_frames(process, log, frames(log), 6)
		assert 'Race music: CD track 5, repeat 0' in log.read_text(), diagnostic(log)

		for button in ('Escape', 'Up'):
			key(race, button, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
		finally:
			xdo('keyup', 'Return')

	reloaded = read_cfg(cfg)
	music_keys = [k for k in saved if k.startswith('CDTrack_')] + ['MenuCDTrack', 'CDLoop']
	assert all(reloaded[k] == saved[k] for k in music_keys), (saved, reloaded)
	print(f'{args.renderer}: restarted previews, race audio and menu return preserve independent choices', flush=True)

	before = cfg.read_bytes()

	for track, song in ((1, 5), (9, 6), (8, 11)):
		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
			'--renderer', args.renderer, '--debug-settings', str(cfg),
			'--debug-car', '1', '--debug-track', str(track), '--debug-check', '--debug-mute', '--debug-no-records',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		result = subprocess.run(drive, env=env, capture_output=True, text=True, timeout=60)
		assert result.returncode == 0 and f'CD track {song}\n' in result.stdout, result

	assert cfg.read_bytes() == before
	capture = root/'sound.bmp'
	result = subprocess.run(command+['--debug-capture', str(capture), '--debug-frames', '3'],
		env=env, capture_output=True, timeout=60)
	assert result.returncode == 0 and capture.is_file(), result.stderr
	assert cfg.read_bytes() == before
	print('Direct race diagnostics resolve Division overrides; menu captures leave CFG untouched', flush=True)

	# Playback goes through the shared settings-aware race loader, without
	# the frontend's transient course array. Bench2 uses Goldbridge.
	values = read_cfg(cfg)
	values['CDTrack_100'] = '7'
	cfg.write_bytes(encode(values))

	with (root/'demo.log').open('w') as stream:
		demo_log = root/'demo.log'
		demo = [str(build_executable(args.build, 'openmotor')), '--debug-demo', str(args.gamedir/'Demos/Bench2.MDE'),
			'--assets', str(args.gamedir), '--debug-settings', str(cfg),
			'--debug-no-records', '--debug-camera-trace', '1', '--debug-window-titles', '1']
		process = subprocess.Popen(demo, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, demo_log, r'\| DEMO')
			wait(process, demo_log, lambda: 'Race music: CD track 7' in demo_log.read_text())
			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(demo_log)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	print('Standalone demo playback uses the saved course music through the shared loader', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('software', 'glide'), default='software')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-course-music-live-') as directory:
		run(args, Path(directory))


if __name__ == '__main__':
	main()
