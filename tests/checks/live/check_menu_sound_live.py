#!/usr/bin/env python3
"""Exercise Main/Options/Sound navigation, audio preview, CFG and course launch under X11."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.input import held


def run(args, root):
	cfg = root / 'test.cfg'
	log = root / 'live.log'
	steps = {'DS3D': 1, 'EAX': 2, 'A3D12': 3, 'A3D20': 4}[args.race_audio]
	cfg.write_bytes(encode(dict(PlayerName='Sound driver', TeamName='NOW',
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'],
		SpatialAudio='Off', GameType='Single', TrackLaps='1', RecordRace='Off', MenuConfirmation='Off',
		ViewDistance='40', SFXVolume='100', MusicVolume='80', MenuCDTrack='3', CDLoop='On', Difficulty='Medium')))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-recordings', str(root / 'recordings'), '--debug-no-records', '--debug-require-audio',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']

	with log.open('w') as out:
		process = subprocess.Popen(command, stdout=out, stderr=out, env=env)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)

			for name in ('Right', 'Right', 'Right', 'Return', 'Right', 'Right', 'Right', 'Return'):
				menu_key(menu, name)

			held(menu, 'Left', .35)  # Effects volume, held independently of repeat events.
			menu_key(menu, 'Down')
			held(menu, 'Left', 2.0)  # Mute music fully.
			assert 'Results music: CD track 2' not in log.read_text()

			# Up from Effects wraps directly to the native race-audio row.
			menu_key(menu, 'Up')
			menu_key(menu, 'Up')

			for _ in range(steps):
				menu_key(menu, 'Right')

			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			text = log.read_text()
			match = re.search(r'effects (\d+), music (\d+), loop (\d+)', text)
			assert match and 0 < int(match[1]) < 255 and match[2] == '0', diagnostic(log)
			saved = read_cfg(cfg)
			assert float(saved['MusicVolume']) == 0, saved
			assert saved['RaceAudio'] == args.race_audio and 'SpatialAudio' not in saved, saved
			assert float(saved['SFXVolume']) < 100, saved
			print('X11 Main -> Options -> Sound held volumes and window-close CFG save passed', flush=True)
		except Exception:
			print(diagnostic(log), flush=True)

			raise
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	# Reload muted preferences, restore menu music on unmute, then edit and
	# audition RedRock (second course after the original Division sort).
	with log.open('w') as out:
		process = subprocess.Popen(command + ['--debug-page', 'sound', '--debug-selection', '1'], stdout=out, stderr=out, env=env)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)
			assert 'Frontend music: CD track' not in log.read_text(), diagnostic(log)
			held(menu, 'Right', .35)
			wait(process, log, lambda: 'Frontend music: CD track 3' in log.read_text())

			for name in ('Down', 'Left', 'Down', 'Down', 'Return'):
				menu_key(menu, name)

			wait(process, log, lambda: 'Frontend music: CD track 4' in log.read_text())

			# The muted frontend did not scan initially; preview populates it.
			menu_key(menu, 'Right')
			menu_key(menu, 'Return')
			wait(process, log, lambda: 'Frontend music: CD track 5' in log.read_text())
			count = log.read_text().count('Frontend music: CD track 5')
			menu_key(menu, 'Return')
			assert log.read_text().count('Frontend music: CD track 5') == count, diagnostic(log)
			menu_key(menu, 'Escape')
			time.sleep(.4)
			assert log.read_text().count('Frontend music: CD track 3') == 2, diagnostic(log)

			# Sound returns to retained Options selection (zero for direct
			# Sound entry). Move to Difficulty, cycle Medium->Hard, return.
			for name in ('Right', 'Right', 'Right', 'Right', 'Return', 'Escape', 'Return', 'Right', 'Return', 'Return'):
				menu_key(menu, name)

			# Main starts at Single; choose Single race beside Quick Race,
			# then traverse Track -> Car. Enter once more launches.
			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)
			assert 'Race music: CD track 5, repeat 0' in log.read_text(), diagnostic(log)
			label = {'DS3D': 'DS3D', 'EAX': 'EAX reverb', 'A3D12': 'A3D 1.2', 'A3D20': 'A3D 2.0'}[args.race_audio]
			assert f'Race audio: Spatial ({label})' in log.read_text(), diagnostic(log)
			assert 'Effects:' not in log.read_text() and 'Audio output stopped:' not in log.read_text(), diagnostic(log)

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			assert 'Results music: CD track 2' not in log.read_text()
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			saved = read_cfg(cfg)
			assert saved['CDLoop'] == 'Off' and saved['Difficulty'] == 'Hard' and float(saved['MusicVolume']) > 0, saved
			assert 'course 1 CD 5' in log.read_text(), diagnostic(log)
			print('X11 muted reload, unmute, course edit/preview, restore, difficulty, race handoff and saved settings passed', flush=True)
		except Exception:
			print(diagnostic(log), flush=True)

			raise
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	with log.open('w') as out:
		process = subprocess.Popen(
			command + ['--debug-page', 'sound', '--debug-selection', '4'],
			stdout=out,
			stderr=out,
			env=env,
		)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)

			for _ in range(steps):
				menu_key(menu, 'Left')

			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert read_cfg(cfg)['RaceAudio'] == 'Software'
			print('Sound menu spatial selection, saved reload, race application and software restore passed', flush=True)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--race-audio', choices=('DS3D', 'EAX', 'A3D12', 'A3D20'), default='EAX')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-sound-live-') as directory:
		run(args, Path(directory))


if __name__ == '__main__':
	main()
