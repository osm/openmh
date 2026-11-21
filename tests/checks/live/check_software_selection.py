#!/usr/bin/env python3
"""Qualify software selection, saved choices and session reentry under X11."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_drive_settings import run_live
from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, race_and_return
from tests.checks.support.settings import encode, read_cfg


def mode_sizes(args, root, env):
	cfg = root/'modes.cfg'
	log = root/'modes.log'
	base = dict(Renderer='Glide', ScreenDimension='1920 1080',
		DetailMode='Custom', MenuConfirmation='Off', RecordRace='Off',
		SFXVolume='0', MusicVolume='0')
	menu_command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--debug-no-records', '--audio', '0', '--debug-window-titles', '1']
	drive_command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--debug-no-records', '--debug-mute', '--debug-window-titles', '1']

	# Switching devices uses the original menu's nearest-mode rule.
	cfg.write_bytes(encode(base))

	with frontend(menu_command, ['--debug-page', 'graphics'], log, env) as (_, menu):
		menu_key(menu, 'Left')

	saved = read_cfg(cfg)
	width, height = map(int, saved['ScreenDimension'].split())
	assert saved['Renderer'] == 'Software' and 320 <= width <= 1023
	run_live(drive_command, env, log)
	assert f'Race viewport: {width}x{height}' in log.read_text(), diagnostic(log)

	for renderer, options in (('Software', []),
		('Glide', ['--renderer', 'software'])):
		cfg.write_bytes(encode(dict(base, Renderer=renderer)))

		with frontend(menu_command, ['--debug-page', 'graphics']+options, log, env):
			pass

		assert read_cfg(cfg)['ScreenDimension'] == '960 640'
		cfg.write_bytes(encode(dict(base, Renderer=renderer)))
		run_live(drive_command+options, env, log)
		assert 'Race viewport: 960x640' in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['Renderer'] == 'Software'
		assert read_cfg(cfg)['ScreenDimension'] == '960 640'

	cfg.write_bytes(encode(dict(base, Renderer='Software', ScreenDimension='1023 768')))
	run_live(drive_command, env, log)
	assert 'Race viewport: 1023x768' in log.read_text(), diagnostic(log)
	assert read_cfg(cfg)['ScreenDimension'] == '1023 768'
	cfg.write_bytes(encode(base))

	with frontend(menu_command, ['--debug-page', 'graphics'], log, env):
		pass

	assert read_cfg(cfg)['ScreenDimension'] == '1920 1080'
	print('X11 renderer modes: wide saved/explicit Software fallback, 1023-wide race, Glide size retention passed', flush=True)


def run(args, root):
	cfg = root/'renderer.cfg'
	cfg.write_bytes(encode(dict(Renderer='Glide', ScreenDimension='640 400',
		DetailMode='Custom', MenuConfirmation='Off', RecordRace='Off',
		SFXVolume='0', MusicVolume='0', TrackLaps='1')))
	recordings = root/'recordings'
	recordings.mkdir()
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	mode_sizes(args, root, env)
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--debug-recordings', str(recordings), '--debug-no-records', '--audio', '0',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	log = root/'menu.log'

	with frontend(command, ['--debug-page', 'graphics'], log, env) as (process, menu):
		menu_key(menu, 'Left')
		race_and_return(process, menu, log, 'Software', restart=True)
		assert read_cfg(cfg)['Renderer'] == 'Software'
		time.sleep(.5)

		for name in ('Right', 'Right', 'Right', 'Return', 'Right', 'Return'):
			menu_key(menu, name)

		menu_key(menu, 'Right')
		race_and_return(process, menu, log, 'Glide')
		assert read_cfg(cfg)['Renderer'] == 'Glide'

	print('X11 Graphics: software race/restart/abort/menu, then Glide race/abort/menu passed', flush=True)

	# An explicit menu override is published; reopening retains its device.
	with frontend(command, ['--debug-page', 'graphics', '--renderer', 'software'], log, env):
		pass

	assert read_cfg(cfg)['Renderer'] == 'Software'

	with frontend(command, ['--debug-page', 'graphics'], log, env) as (process, menu):
		race_and_return(process, menu, log, 'Software')

	assert read_cfg(cfg)['Renderer'] == 'Software'

	drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir), '--debug-no-records', '--debug-mute',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']

	for options, profile in ((['--debug-settings', str(cfg)], 'Software'),
		(['--debug-settings', str(cfg), '--renderer', 'glide'], 'Glide'),
		(['--debug-no-settings', '--renderer', 'software'], 'Software')):
		run_live(drive+options, env, root/'drive.log')
		assert f'Race renderer: {profile}' in (root/'drive.log').read_text()

	assert read_cfg(cfg)['Renderer'] == 'Glide'
	print('X11 saved renderer reload and CLI precedence passed', flush=True)

	for selector in (['--debug-race', '--assets', str(args.gamedir)], []):
		result = subprocess.run([str(build_executable(args.build, 'openmotor')), *selector, '--renderer', 'invalid'],
			env=env, capture_output=True, timeout=30)
		assert result.returncode != 0

	# Six session modes cancelled before/after a frame, plus rejected pacing.
	result = subprocess.run([str(build_executable(args.build, 'mh-session-lifecycle')), str(args.gamedir),
		str(recordings), 'software'], env=env, capture_output=True, text=True, timeout=180)
	assert result.returncode == 0, result.stdout+result.stderr
	assert result.stdout.count('Race renderer: Software') == 13, result.stdout
	print('Software single/ghost/demo/attract cancellation, resource cleanup and reentry passed', flush=True)

	for program in ('mh-network-frame-assets', 'mh-league-driving-assets'):
		for renderer in ('glide', 'software'):
			result = subprocess.run([str(build_executable(args.build, program)), str(args.gamedir), renderer],
				env=env, capture_output=True, text=True, timeout=180)
			assert result.returncode == 0, result.stdout[-4000:]+result.stderr

	print('Saved renderer reaches league and network asset/HUD loaders for both profiles', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-software-selection-') as directory:
		run(args, Path(directory))


if __name__ == '__main__':
	main()
