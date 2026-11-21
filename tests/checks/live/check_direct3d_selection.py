#!/usr/bin/env python3
"""Exercise Direct3D menu selection, saved reload, race reentry and resize on X11."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.live.check_drive_settings import run_live
from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, race_and_return
from tests.checks.support.settings import encode, read_cfg
from tests.checks.live.check_presentation_live import race


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-direct3d-selection-') as directory:
		root = Path(directory)
		cfg, log = root / 'settings.cfg', root / 'live.log'
		recordings = root / 'recordings'
		recordings.mkdir()
		cfg.write_bytes(encode(dict(Renderer='Glide', ScreenDimension='640 400',
			DetailMode='Custom', MenuConfirmation='Off', RecordRace='Off', TrackLaps='1',
			CarShading='Glenz', TexturePrecision='16', FrameQueue='2',
			SFXVolume='0', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		common = ['--assets', str(args.gamedir)]
		menu = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0',
			'--debug-recordings', str(recordings), '--debug-camera-trace', '1', '--debug-window-titles', '1']

		with frontend(menu, ['--debug-page', 'graphics'], log, env) as (process, window):
			menu_key(window, 'Right')
			race_and_return(process, window, log, 'Direct3D', restart=True)
			assert read_cfg(cfg)['Renderer'] == 'Direct3D'

		print('X11 Graphics -> Direct3D: eight-car race, restart and return to menu pass', flush=True)

		# No override: reopening must retain Direct3D before loading the next race.
		with frontend(menu, ['--debug-page', 'graphics'], log, env) as (process, window):
			race_and_return(process, window, log, 'Direct3D')

		assert read_cfg(cfg)['Renderer'] == 'Direct3D'

		for profile, saved in (('Glide', 'Glide'), ('Software', 'Software')):
			with frontend(menu, ['--debug-page', 'graphics'], log, env) as (process, window):
				menu_key(window, 'Left')
				race_and_return(process, window, log, profile)

			assert read_cfg(cfg)['Renderer'] == saved
			assert read_cfg(cfg)['TexturePrecision'] == '16'

		print('Saved Direct3D reload, then Glide and Software race reentry pass', flush=True)

		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race', *common,
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-car', '1', '--debug-track', '1',
			'--debug-opponents', '7', '--debug-mute', '--debug-camera-trace', '1', '--debug-window-titles', '1']
		run_live(drive + ['--renderer', 'direct3d'], env, log)
		assert 'Race renderer: Direct3D' in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['Renderer'] == 'Direct3D'
		race(drive, env, log, '2')
		assert 'Race renderer: Direct3D' in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['Renderer'] == 'Direct3D'
		print('CLI Direct3D override/save, saved direct launch and window resize pass', flush=True)

		result = subprocess.run([str(build_executable(args.build, 'mh-session-lifecycle')),
			str(args.gamedir), str(recordings), 'direct3d'], env=env,
			capture_output=True, text=True, timeout=180)
		assert result.returncode == 0, result.stdout[-5000:] + result.stderr

		# Six session modes, each cancelled before/after a frame, plus the runner.
		assert result.stdout.count('Race renderer: Direct3D') == 13, result.stdout

		for program in ('mh-network-frame-assets', 'mh-league-driving-assets'):
			result = subprocess.run([str(build_executable(args.build, program)), str(args.gamedir), 'direct3d'],
				env=env, capture_output=True, text=True, timeout=180)
			assert result.returncode == 0, result.stdout[-5000:] + result.stderr

		print('Direct3D single/ghost/demo/attract cancellation, league/network assets and cleanup pass', flush=True)


if __name__ == '__main__':
	main()
