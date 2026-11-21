#!/usr/bin/env python3
"""Check global audio mute, saved volumes and startup without an audio device."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.build import build_executable
from tests.checks.support.menu import diagnostic, menu_key, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import frames, wait_frames


def startup_without_device(command, env, root):
	unavailable = dict(env, SDL_AUDIO_DRIVER='openmh-unavailable-device')

	for name, audio, success in (
		('default', [], True),
		('enabled', ['--audio', '1'], True),
		('muted', ['--audio', '0'], True),
		('required', ['--debug-require-audio'], False),
	):
		result = subprocess.run(command + audio + ['--debug-frames', '2'],
			env=unavailable, capture_output=True, text=True,
			timeout=30)
		(root / f'{name}.log').write_text(result.stdout + result.stderr)
		assert (result.returncode == 0) == success, (name, result.stderr)

		if name == 'muted':
			assert 'Frontend audio:' not in result.stderr, result.stderr

		if name == 'required':
			assert 'frontend audio unavailable' in result.stderr, result.stderr

	print('Default/enabled startup tolerates an unavailable device; mute skips it; debug requirement fails.', flush=True)


def race_audio(command, env, root, enabled):
	cfg = root / f'race-{enabled}.cfg'
	log = root / f'race-{enabled}.log'
	cfg.write_text('SFXVolume 100\nMusicVolume 80\nRecordRace Off\n'
		'MenuConfirmation Off\nDetailMode Custom\n'
		'ScreenDimension 640 400\n')
	options = ['--audio', str(enabled), '--debug-settings', str(cfg),
		'--debug-page', 'car', '--debug-car', '1', '--debug-track', '1',
		'--debug-mode', '2', '--debug-record', '0',
		'--debug-camera-trace', '1']

	with frontend(command, options, log, env) as (process, menu):
		menu_key(menu, 'Return')
		window_for(process, log, r'TIME ATTACK \| 1 cars')
		wait_frames(process, log, frames(log), 6)
		assert ('Race audio:' in log.read_text()) == bool(enabled), diagnostic(log)

	saved = read_cfg(cfg)
	volumes = (saved['SFXVolume'], saved['MusicVolume'])
	assert all(float(volume) > 0 for volume in volumes), saved
	print(f'Audio {enabled}: menu-to-race transition passed.', flush=True)

	return volumes


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-audio-toggle-') as temporary:
		root = Path(temporary)
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1',
			'--assets', str(args.gamedir), '--renderer', 'software',
			'--debug-no-records', '--debug-settings', str(root / 'startup.cfg'),
			'--debug-recordings', str(root / 'recordings')]
		startup_without_device(command, env, root)
		muted = race_audio(command, env, root, 0)
		enabled = race_audio(command, env, root, 1)

		# Compare saved values after the game's percentage/byte conversion.
		assert muted == enabled, (muted, enabled)
		print('Muted and enabled runs retain identical nonzero saved volumes.', flush=True)


if __name__ == '__main__':
	main()
