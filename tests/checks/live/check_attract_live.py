#!/usr/bin/env python3
"""Real X11 idle launch, complete demo, repeat and manual/window return.

Uses original wall-clock delays and the actual eight-AI simulation. Run under
xvfb-run. Temporary per-user storage isolates all preferences and records.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, xdo


def launch_count(log):
	return log.read_text().count('Attract launch:')


def remain_in_menu(process, log, launches, seconds):
	deadline = time.monotonic()+seconds

	while time.monotonic() < deadline:
		assert process.poll() is None and launch_count(log) == launches, diagnostic(log)
		time.sleep(.1)


def check_dialog(args):
	# Idle expiry queues attract mode while the exit dialog stays active.
	# The race starts only after the player dismisses the dialog.
	for answer in ('cancel', 'accept'):
		with tempfile.TemporaryDirectory(prefix='mh-attract-dialog-') as temporary:
			root = Path(temporary)
			cfg = root/'MotorHead.CFG'
			log = root/'live.log'
			cfg.write_bytes(encode(dict(MenuConfirmation='On', MusicVolume='0')))
			env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
			command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
				'--debug-settings', str(cfg), '--debug-no-records', '--debug-window-titles', '1']

			with log.open('w') as stream:
				process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

				try:
					menu = window_for(process, log, 'main-menu preview')
					xdo('windowfocus', '--sync', menu)
					menu_key(menu, 'Escape')
					remain_in_menu(process, log, 0, 35)

					if answer == 'cancel':
						menu_key(menu, 'Escape')
					else:
						menu_key(menu, 'Left')
						menu_key(menu, 'Return')

					# Even a Yes answer now belongs to the modal on layout
					# 28, so Main's retained exit answer is not consumed.
					wait(process, log, lambda: launch_count(log) == 1, 15)
					race = window_for(process, log, r'Motorhead \| DEMO')
					close_window(race)
					assert process.wait(timeout=30) == 0, diagnostic(log)
					assert 'Attract return: manual 0, quit 1' in log.read_text(), diagnostic(log)
					print(f'Attract dialog: pending idle launch waits for {answer}, then starts', flush=True)
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


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--dialog-only', action='store_true', help='check idle timeout during exit confirmation')
	args = parser.parse_args()

	if args.dialog_only:
		check_dialog(args)

		return

	with tempfile.TemporaryDirectory(prefix='mh-attract-live-') as temporary:
		root = Path(temporary)
		cfg = root/'MotorHead.CFG'
		log = root/'live.log'
		records = root/'records'
		recordings = root/'recordings'
		records.mkdir()
		recordings.mkdir()
		cfg.write_bytes(encode(dict(PlayerName='Demo observer', TeamName='KEEP', GameType='TimeAttack',
			RecordRace='On', Difficulty='Easy', CatchUp='On', TrackLaps='3',
			MenuConfirmation='Off', DetailMode='Custom', InfoDetail='Full', CarShading='Flat',
			ViewDistance='100', SFXVolume='35', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
			'--debug-settings', str(cfg), '--debug-records', str(records), '--debug-recordings', str(recordings),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				xdo('windowfocus', '--sync', menu)
				remain_in_menu(process, log, 0, 20)

				# An accepted key with no menu action renews the full 30s.
				menu_key(menu, 'Shift_L')
				reset = time.monotonic()
				remain_in_menu(process, log, 0, 12)
				wait(process, log, lambda: launch_count(log) == 1, 30)
				assert time.monotonic()-reset >= 28, diagnostic(log)
				print('Attract idle: accepted key renewed the original 30-second delay', flush=True)
				race = window_for(process, log, r'Motorhead \| DEMO')
				xdo('windowfocus', '--sync', race)
				baseline = cfg.read_bytes()
				saved = read_cfg(cfg)
				assert saved['GameType'] == 'TimeAttack' and saved['RecordRace'] == 'On', saved
				assert saved['Difficulty'] == 'Easy' and saved['TrackLaps'] == '3', saved
				wait(process, log, lambda: 'Attract return: manual 0, quit 0' in log.read_text(), 85)
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
				returned = time.monotonic()
				assert cfg.read_bytes() == baseline, read_cfg(cfg)
				assert not list(records.iterdir()) and not list(recordings.iterdir())

				# Both camera halves occur in a real run, and the next demo
				# chooses a different eligible track after the shorter delay.
				cameras = [line.split() for line in log.read_text().splitlines()
					if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:')]

				for start,end,mode in ((13000,21000,1),(23000,31000,2),(33000,41000,1)):
					selected = [row for row in cameras if start < float(row[16]) < end]
					assert selected and all(int(row[5]) == mode for row in selected), (start,selected[-3:])

				print('Attract race: cockpit/track cycles, timed return and unchanged storage passed', flush=True)
				wait(process, log, lambda: launch_count(log) == 2, 25)
				assert 8 <= time.monotonic()-returned < 25, diagnostic(log)
				tracks = re.findall(r'Attract launch: track (\d+)', log.read_text())
				assert len(tracks) == 2 and tracks[0] != tracks[1], tracks

				# Car shading is restored from the race snapshot and saved
				# on the next departure. Other demo overrides do not leak.
				restored = read_cfg(cfg)
				assert restored == dict(saved,CarShading='Reflection'), restored
				baseline = cfg.read_bytes()
				race = window_for(process, log, r'Motorhead \| DEMO')
				xdo('windowfocus', '--sync', race)
				time.sleep(.5)
				xdo('keydown', 'Escape')

				try:
					wait(process, log, lambda: 'Attract return: manual 1, quit 0' in log.read_text())
				finally:
					xdo('keyup', 'Escape')

				wait(process, log, lambda: log.read_text().count('Frontend resumed: main') == 2)
				assert cfg.read_bytes() == baseline
				remain_in_menu(process, log, 2, 12)
				wait(process, log, lambda: launch_count(log) == 3, 35)
				race = window_for(process, log, r'Motorhead \| DEMO')
				close_window(race)
				assert process.wait(timeout=30) == 0, diagnostic(log)
				assert 'Attract return: manual 0, quit 1' in log.read_text(), diagnostic(log)
				assert cfg.read_bytes() == baseline
				assert not list(records.iterdir()) and not list(recordings.iterdir())
				assert not list(root.rglob('*.mhi')) and not list(root.rglob('*.mde'))
				print('X11 attract: key-renewed 30s idle, full eight-AI demo, alternate track after 10s, '
					'Escape restoring 30s, window close, selective CFG return and untouched records/recordings passed', flush=True)
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


if __name__ == '__main__':
	main()
