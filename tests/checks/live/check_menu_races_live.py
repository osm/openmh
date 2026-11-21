#!/usr/bin/env python3
"""Exercise frontend -> Quick Race/Time Attack -> frontend with real X11 input.

Run under xvfb-run. Uses isolated CFG, records and recordings, dummy SDL audio,
and existing camera diagnostics to synchronize input with slow renderers.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.installation import prepare_view
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.catalog import fields
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.menu import diagnostic, wait, menu_key, save_recording, window_for, intro_ms, recordable


from tests.checks.support.process import COMMAND_TIMEOUT


def default_storage(args):
	with tempfile.TemporaryDirectory(prefix='mh-menu-storage-') as temporary:
		root = Path(temporary)
		game = root / 'game'
		prepare_view(game, args.gamedir)
		log = root/'live.log'
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		with log.open('w') as stream:
			command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game),
				'--debug-no-settings', '--debug-no-records', '--audio', '0', '--debug-page', 'car',
				'--debug-mode', '2', '--debug-record', '1', '--debug-confirmation', '0', '--debug-name', 'No CFG driver',
				'--debug-camera-trace', '1',
				'--debug-window-titles', '1']

			if getattr(args, 'renderer', None):
				command += ['--renderer', args.renderer]

			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)
				menu_key(menu, 'Return')
				race = window_for(process, log, r'TIME ATTACK \| 1 cars')
				xdo('windowfocus', '--sync', race)
				recordable(process, log)

				for name in ('Escape', 'Up'):
					key(race, name, process, log)

				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: 'Frontend resumed: recording save' in log.read_text())
				finally:
					xdo('keyup', 'Return')

				save_recording(process, log, menu, 'Default take')
				close_window(menu)
				assert process.wait(timeout=45) == 0, diagnostic(log)
				take = game/'userdata/default take.mde'
				assert take.is_file(), diagnostic(log)
				decoded = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(take)], text=True, timeout=COMMAND_TIMEOUT)
				assert '"No CFG driver"' in decoded and 'validated ' in decoded
				assert 'simulation timing=1 rate=120' in decoded
				assert not list(root.rglob('motorhead.cfg')) and not list(root.rglob('*.tmp.*'))
				print('Native storage: default recording directory works with settings/record book disabled', flush=True)
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
	parser.add_argument('--storage-only', action='store_true')
	args = parser.parse_args()

	if args.storage_only:
		default_storage(args)

		return

	cars = {i: fields(args.gamedir/'Game'/f'car{i:02}.car')['carname'] for i in range(1, 11)}
	tracks = {i: fields(args.gamedir/'Game'/f'Track{i}.trk')['trackname'] for i in range(1, 9)}

	with tempfile.TemporaryDirectory(prefix='mh-menu-races-') as temporary:
		root = Path(temporary)
		cfg = root/'MotorHead.CFG'
		log = root/'live.log'
		recordings = root/'recordings'
		recordings.mkdir()
		(root/'records').mkdir()
		protected = recordings/'race0001.mde'
		protected.write_bytes(b'preserve existing recording')
		cfg.write_bytes(encode(dict(PlayerName='Menu driver', TeamName='NOW', CarName=cars[2],
			TrackName=tracks[8], GameType='Single', TrackLaps='1', CatchUp='Off',
			CarTransmission='Auto', CarHorn='Honker1', RecordRace='Off', Difficulty='Medium',
			MenuConfirmation='Off', DetailMode='Custom', ViewDistance='100', SFXVolume='35', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--user-data', str(root / 'userdata'), '--debug-settings', str(cfg),
			'--debug-records', str(root/'records'), '--debug-recordings', str(recordings), '--debug-require-audio', '--debug-page', 'setup',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)

				# Track 8 -> 1, then car 2 -> 7, Manual, next horn, Record On.
				for name in ('Right', 'Return', 'Right', 'Down', 'Left', 'Down', 'Right', 'Down', 'Right', 'Return'):
					menu_key(menu, name)

				race = window_for(process, log, r'\| 8 cars \|')
				xdo('windowfocus', '--sync', race)
				wait_frames(process, log, frames(log), 6)
				saved = read_cfg(cfg)
				assert saved['CarName'] == cars[7] and saved['TrackName'] == tracks[1], saved
				assert saved['RecordRace'] == 'On' and saved['CarTransmission'] == 'Manual', saved
				assert 'Race launch: mode 0, car 7, track 1, laps 1, entrants 8, human slot 7' in log.read_text()

				# Enough unpaused render updates to publish an MDE take.
				recordable(process, log)

				for name in ('Escape', 'Down', 'Down', 'Return', 'Down', 'Down', 'Left', 'Escape', 'Escape'):
					key(race, name, process, log)

				for name in ('Escape', 'Up'):
					key(race, name, process, log)

				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: 'Frontend resumed: recording save' in log.read_text())
				finally:
					xdo('keyup', 'Return')

				assert 'Race return: completed 1, quit 0' in log.read_text()
				assert window_for(process, log, 'main-menu preview') == menu, 'frontend window was replaced'
				assert float(read_cfg(cfg)['ViewDistance']) == 100, 'return saved before the next frontend departure'
				assert not list(recordings.glob('*.mde')), 'take published before save decision'
				save_recording(process, log, menu, 'Quick take')
				take = recordings/'quick take.mde'
				assert take.exists() and protected.read_bytes() == b'preserve existing recording'
				decoded = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(take)], text=True, timeout=COMMAND_TIMEOUT)
				assert '"Menu driver" "NOW" "'+cars[7]+'"' in decoded, decoded[:2000]
				assert f'track "{tracks[1]}"' in decoded and 'validated ' in decoded, decoded[:2000]
				original_take = take.read_bytes()
				print('Quick Race: selected assets/profiles, launch-time CFG, record preservation and return passed', flush=True)

				# Reuse the live frontend, select Time Attack, launch from car setup.
				time.sleep(.6)

				for name in ('Return', 'Right', 'Right', 'Right', 'Return', 'Return', 'Return'):
					menu_key(menu, name)

				race = window_for(process, log, r'TIME ATTACK \| 1 cars')
				xdo('windowfocus', '--sync', race)
				wait_frames(process, log, frames(log), 6)
				assert float(read_cfg(cfg)['ViewDistance']) == 95, read_cfg(cfg)
				wait(process, log, lambda: intro_ms(log) > 7500)

				# Restart from the original pause menu, then discard the short new take.
				for name in ('Escape', 'Down'):
					key(race, name, process, log)

				key(race, 'Return', process, log)
				assert intro_ms(log) < 6000, 'pause restart did not reset countdown'
				wait_frames(process, log, frames(log), 6)  # Original reset suppresses four input polls.

				for name in ('Escape', 'Up'):
					key(race, name, process, log)

				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: log.read_text().count('Frontend resumed: main') == 2)
				finally:
					xdo('keyup', 'Return')

				assert not (recordings/'race0003.mde').exists(), 'short restarted take was published'
				assert take.read_bytes() == original_take
				assert read_cfg(cfg)['GameType'] == 'TimeAttack'
				assert log.read_text().count('Race return: completed 1, quit 0') == 2
				print('Time Attack: second launch, original restart action and short-recording discard passed', flush=True)

				# Current-mode Quick shortcut; turn recording off in car setup first.
				time.sleep(.6)

				for name in ('Return', 'Return', 'Return', 'Left', 'Escape', 'Escape',
					'Left', 'Left', 'Left', 'Return'):
					menu_key(menu, name)

				race = window_for(process, log, r'TIME ATTACK \| 1 cars')
				wait_frames(process, log, frames(log), 6)
				close_window(race)
				assert process.wait(timeout=45) == 0, log.read_text()[-12000:]
				assert 'Race return: completed 0, quit 1' in log.read_text()
				assert read_cfg(cfg)['RecordRace'] == 'Off'
				assert len(list(recordings.iterdir())) == 2, list(recordings.iterdir())
				assert not list(root.rglob('*.tmp.*'))
				assert log.read_text().count('Frontend resumed: main') == 2
				print('Quick shortcut: retains Time Attack mode, Record Off and window-close exit passed', flush=True)
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

	default_storage(args)


if __name__ == '__main__':
	main()
