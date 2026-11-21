#!/usr/bin/env python3
"""Real frontend CFG reload and selective snapshot restoration across races.

Edits only a private fixture CFG while its race runs. Confirms original
save-before-preset, no premature return save, and refreshed menu consumers.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.catalog import fields
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	cars = {i: fields(args.gamedir/'Game'/f'car{i:02}.car')['carname'] for i in (1, 2)}
	tracks = {i: fields(args.gamedir/'Game'/f'Track{i}.trk')['trackname'] for i in (1, 8)}

	with tempfile.TemporaryDirectory(prefix='mh-frontend-return-') as temporary:
		root = Path(temporary)
		cfg = root/'MotorHead.CFG'
		log = root/'live.log'
		subprocess.run([str(build_executable(args.build, 'mh-settings-store-assets')), str(args.gamedir), str(root/'store.CFG')], check=True, timeout=COMMAND_TIMEOUT)
		cfg.write_bytes(encode(dict(PlayerName='Initial driver', TeamName='OLD', CarName=cars[1], TrackName=tracks[1],
			GameType='TimeAttack', TrackLaps='1', RecordRace='Off', MenuConfirmation='Off',
			DetailMode='High', CarShading='Flat', Perspective='Good', ViewDistance='42',
			Smoke='Off', Shadow='Off', Halos='Off', SFXVolume='35', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
			'--debug-settings', str(cfg), '--debug-no-records', '--debug-page', 'car',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)
				menu_key(menu, 'Return')
				race = window_for(process, log, r'TIME ATTACK \| 1 cars')
				xdo('windowfocus', '--sync', race)
				wait_frames(process, log, frames(log), 6)
				saved = read_cfg(cfg)
				assert saved['Smoke'] == 'Off' and saved['CarShading'] == 'Flat' and saved['ViewDistance'] == '42.00', saved
				assert saved['Perspective'] == 'Good', saved

				# A real reread must discard runtime-only preset values and
				# refresh the selected assets and pages from this file.
				changed = dict(saved, PlayerName='Reloaded driver', TeamName='NEW', CarName=cars[2], TrackName=tracks[8],
					TrackLaps='3', Difficulty='Hard', DetailMode='Custom', CarShading='Gouraud', Perspective='Good',
					ViewDistance='47.25', SFXVolume='0', MusicVolume='31', CDLoop='Off',
					CheckpointDelay='1.13', Smoke='Off', Shadow='Off', Halos='Off')
				cfg.write_bytes(encode(changed))
				external = cfg.read_bytes()

				for name in ('Escape', 'Up'):
					key(race, name, process, log)

				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
				finally:
					xdo('keyup', 'Return')

				assert cfg.read_bytes() == external, 'return overwrote CFG before the next departure'
				menu_key(menu, 'Return')
				menu_key(menu, 'Return')
				wait(process, log, lambda: log.read_text().count('Race launch:') == 2)
				race = window_for(process, log, r'TIME ATTACK \| 1 cars')
				assert 'Race launch: mode 2, car 2, track 8, laps 3, entrants 1' in log.read_text(), diagnostic(log)
				returned = read_cfg(cfg)
				assert returned['PlayerName'] == 'Reloaded driver' and returned['TeamName'] == 'NEW', returned
				assert returned['Difficulty'] == 'Hard' and returned['DetailMode'] == 'Custom', returned
				assert all(returned[name] == 'Off' for name in ('Smoke', 'Shadow', 'Halos', 'CDLoop', 'RecordRace')), returned
				assert returned['CheckpointDelay'] == '1.13', returned

				# Five snapshot options win over the reloaded file. The
				# first race's High preset set Reflection/Normal/80.
				assert returned['CarShading'] == 'Reflection' and returned['Perspective'] == 'Normal', returned
				assert returned['ViewDistance'] == '80.00', returned
				assert returned['SFXVolume'] == saved['SFXVolume'] and returned['MusicVolume'] == saved['MusicVolume'], returned
				close_window(race)
				assert process.wait(timeout=30) == 0, diagnostic(log)
				assert read_cfg(cfg)['PlayerName'] == 'Reloaded driver'
				print('Frontend return: actual CFG reload, five snapshot overrides, preset isolation, deferred save and refreshed race selections passed', flush=True)
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
