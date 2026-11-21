#!/usr/bin/env python3
"""Exercise openmotor --debug-race's real CFG/CLI/exit behavior on an existing X11 display.

Run under xvfb-run. Requires xdotool and the extracted game assets. All writes
are confined to a temporary per-user data directory; the game installation
is read-only. The separate mh-pause-live fixture tests individual option edits.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time


from tests.checks.support.x11 import close_window


from tests.checks.support.process import COMMAND_TIMEOUT


def run_live(command, environment, log):
	with log.open('wb') as output:
		process = subprocess.Popen(command, env=environment, cwd=log.parent, stdout=output, stderr=subprocess.STDOUT)

		try:
			window = subprocess.check_output(['xdotool', 'search', '--sync', '--pid', str(process.pid),
				'--name', 'Motorhead'], timeout=30).splitlines()[-1].decode()
			deadline = time.monotonic()+30

			while True:
				title = subprocess.check_output(['xdotool', 'getwindowname', window], timeout=COMMAND_TIMEOUT).decode()

				# Loading and racing share this window. Wait for the race's
				# first status title before sending the close request.
				if ' cars |' in title:
					break

				assert process.poll() is None and time.monotonic() < deadline, log.read_text()
				time.sleep(.05)

			close_window(window)
			assert process.wait(timeout=30) == 0, log.read_text()
		finally:
			if process.poll() is None:
				process.terminate()
				process.wait(timeout=10)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir.resolve()), '--debug-car', '1', '--debug-track', '1', '--debug-no-records',
		'--debug-window-titles', '1']

	with tempfile.TemporaryDirectory(prefix='openmh-race-settings-') as temporary:
		root = Path(temporary)
		environment = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		settings = root/'explicit.cfg'
		original = ('PlayerName Saved Driver\nTeamName TEST\nShortKey9 preserve this text\n'
			'SessionPassword keepme\nCarTransmission Manual\nDetailMode Custom\n'
			'CarShading Gouraud\nCarDetail Low\nTrackDetail Medium\n'
			'Perspective Normal\nViewDistance 125\nSFXVolume 100.1\nMusicVolume 34.12\n')
		settings.write_text(original)
		run_live(command+['--debug-settings', str(settings), '--debug-volume', '20', '--debug-music-volume', '0'], environment, root/'first.log')
		assert 'Ambience disabled:' not in (root/'first.log').read_text()
		saved = settings.read_bytes()
		assert b'SFXVolume\t20.00\n' in saved and b'MusicVolume\t0.00\n' in saved, saved

		for key, value in [('PlayerName', 'Saved Driver'), ('TeamName', 'TEST'), ('ShortKey9', 'preserve this text'),
			('SessionPassword', 'keepme'), ('CarTransmission', 'Manual'), ('CarShading', 'Gouraud'),
			('Perspective', 'Normal'), ('ViewDistance', '125.00'), ('DetailMode', 'Custom'),
			('CarDetail', 'Low'), ('TrackDetail', 'Medium')]:
			assert f'{key}\t{value}\n'.encode() in saved, (key, saved)

		inode = settings.stat().st_ino
		run_live(command+['--debug-settings', str(settings)], environment, root/'second.log')
		assert settings.read_bytes() == saved and settings.stat().st_ino == inode, 'unchanged run rewrote preferences'

		# Diagnostic runs must not create defaults or rewrite an explicit file.
		headless = dict(environment, SDL_VIDEO_DRIVER='unavailable', DISPLAY='')
		subprocess.run(command+['--debug-check', '--debug-settings', str(settings), '--debug-volume', '50'], env=headless, cwd=root,
			check=True, stdout=subprocess.DEVNULL, timeout=30)
		assert settings.read_bytes() == saved and settings.stat().st_ino == inode
		subprocess.run(command+['--debug-check', '--debug-no-settings'], env=headless, cwd=root, check=True, stdout=subprocess.DEVNULL, timeout=30)
		preferred = root/'userdata/motorhead.cfg'
		assert not preferred.exists()

		for renderer in ('glide', 'software'):
			run_live(command+['--debug-no-settings', '--renderer', renderer, '--debug-opponents', '7', '--debug-mute'],
				environment, root/f'{renderer}.log')
			assert 'Race viewport: 960x640' in (root/f'{renderer}.log').read_text()
			assert not preferred.exists()

		run_live(command, environment, root/'default.log')
		assert preferred.is_file() and b'ViewDistance\t80.00\n' in preferred.read_bytes()
		malformed = b'CarHorn ABCD\nSFXVolume 1e99999\n'
		settings.write_bytes(malformed)
		failed = subprocess.run(command+['--debug-check', '--debug-settings', str(settings)], env=environment, cwd=root,
			stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30)
		assert failed.returncode != 0 and settings.read_bytes() == malformed
		assert b'invalid' in failed.stderr

	print('openmotor --debug-race: real X11 exits, CFG/CLI volume precedence, metadata preservation, unchanged-file stability, working-directory defaults and diagnostic read-only behavior passed')


if __name__ == '__main__':
	main()
