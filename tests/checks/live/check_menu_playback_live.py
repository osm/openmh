#!/usr/bin/env python3
"""Exercise the frontend Ghost/Demo handoffs under xvfb-run with private files.

The short demo keeps 64 unmodified shipped frames. EOF must retain the pose,
not end the session. The finish case keeps the complete stream and changes
only the private copy's lap count to one. Original resources stay untouched.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_ghost_preview import fixture, snapshot
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, intro_ms, menu_key, recordable, save_recording, ticks, wait, window_for
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def quit_race(process, log, window, recording=False):
	marker = 'Frontend resumed: recording save' if recording else 'Frontend resumed: main'
	returned = log.read_text().count(marker)

	for name in ('Escape', 'Up'):
		key(window, name, process, log)

	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: log.read_text().count(marker) > returned)
	finally:
		xdo('keyup', 'Return')

	assert 'Playback return: completed 1, quit 0' in log.read_text(), diagnostic(log)


def finish(args):
	with tempfile.TemporaryDirectory(prefix='mh-menu-demo-finish-') as temporary:
		root = Path(temporary)
		cfg = root/'private.cfg'
		log = root/'live.log'
		game = root/'game'
		demos = fixture(game, args.gamedir, ['Private'])
		data = bytearray((args.gamedir/'Demos/JeePen.MDE').read_bytes())
		struct.pack_into('<I', data, 24, 1)
		(demos/'Private.MDE').write_bytes(data)
		before = snapshot(demos)
		cfg.write_bytes(encode(dict(CarHorn='Honker1', GameType='GhostMode',
			DemoFileName='Private', RecordRace='On', MenuConfirmation='Off',
			ViewDistance='40', SFXVolume='37' if args.sound else '0', MusicVolume='0')))
		recordings = root/'recordings'
		records = root/'records'
		recordings.mkdir()
		records.mkdir()
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game),
			'--debug-settings', str(cfg), '--debug-records', str(records), '--debug-recordings', str(recordings),
			'--audio', '0', '--debug-page', 'ghost', '--debug-camera-trace', '1', '--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)

				for name in ('Down', 'Right', 'Return'):
					menu_key(menu, name)

				race = window_for(process, log, r'DEMO \| 1 cars')
				xdo('windowfocus', '--sync', race)
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text(), seconds=150)
				assert 'Playback launch: mode 6, track NeoCity, laps 1, entrants 1, recording 0' in log.read_text()
				assert 'Playback return: completed 1, quit 0' in log.read_text(), diagnostic(log)
				assert 'Race results (1/1 finished)' in log.read_text(), diagnostic(log)

				# A real crossing ends the lap. EOF is much later; no input
				# requests exit, and the original ten-second delay still runs.
				assert 70000 < intro_ms(log) < 120000, diagnostic(log)
				assert window_for(process, log, 'main-menu preview') == menu
				assert read_cfg(cfg)['GameType'] == 'TimeAttack'
				print('demo-finish: one-lap playback returned automatically to the retained menu', flush=True)

				for name in ('Return', 'Right', 'Right', 'Right', 'Right', 'Return',
					'Down', 'Right', 'Return'):
					menu_key(menu, name)

				race = window_for(process, log, r'DEMO \| 1 cars')
				xdo('windowfocus', '--sync', race)
				wait_frames(process, log, frames(log), 6)
				wait(process, log, lambda: intro_ms(log) > 7000)
				before_restart = ticks(log)

				for button in ('Escape', 'Down', 'Return'):
					key(race, button, process, log)

				assert intro_ms(log) < 6000 and ticks(log) >= before_restart, diagnostic(log)
				wait_frames(process, log, frames(log), 6)
				quit_race(process, log, race)
				wait(process, log, lambda: log.read_text().count('Playback return: completed 1, quit 0') == 2)
				assert log.read_text().count('Frontend resumed: main') == 2, diagnostic(log)
				assert not list(recordings.iterdir()) and not list(records.iterdir())
				assert snapshot(demos) == before and not list(root.rglob('*.tmp.*'))

				if args.sound:
					for marker in ('Audio disabled:', 'Audio:', 'Message sound:', 'Effects:'):
						assert marker not in log.read_text(), diagnostic(log)

				close_window(menu)
				assert process.wait(timeout=45) == 0, diagnostic(log)
				print('demo-finish: second launch, Home, abort and recording suppression passed', flush=True)
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


def run(args, mode):
	with tempfile.TemporaryDirectory(prefix='mh-menu-playback-') as temporary:
		root = Path(temporary)
		cfg = root/'private.cfg'
		log = root/'live.log'
		game = root/'game'
		demos = fixture(game, args.gamedir, ['Private'])
		original = (args.gamedir/'Demos/JeePen.MDE').read_bytes()
		count = struct.unpack_from('<I', original, 28)[0]

		if mode != 'invalid':
			(demos/'Private.MDE').write_bytes(original[:4012+64*(8+30*count)])

		before = snapshot(demos)
		car = fields(args.gamedir/'Game/car02.car')['carname']
		cfg.write_bytes(encode(dict(PlayerName='Ghost driver', TeamName='TEST', CarName=car,
			GameType='GhostMode', DemoFileName='Private', CarTransmission='Manual',
			CarHorn='Honker1', RecordRace='On', MenuConfirmation='Off',
			ViewDistance='40', SFXVolume='37' if args.sound else '0', MusicVolume='0')))
		recordings = root/'recordings'
		records = root/'records'
		recordings.mkdir()
		records.mkdir()
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game), '--debug-settings', str(cfg),
			'--debug-records', str(records), '--debug-recordings', str(recordings), '--audio', '0', '--debug-page', 'ghost',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)
				ghost = mode in ('ghost', 'close')

				for name in (('Right', 'Return', 'Return') if ghost else ('Down', 'Right', 'Return')):
					menu_key(menu, name)

				if mode == 'invalid':
					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
					assert 'Playback: truncated demo header' in log.read_text(), diagnostic(log)
					assert window_for(process, log, 'main-menu preview') == menu
					menu_key(menu, 'Escape')
				else:
					race = window_for(process, log, r'GHOST \| 2 cars' if ghost else r'DEMO \| 1 cars')
					xdo('windowfocus', '--sync', race)
					wait_frames(process, log, frames(log), 6)
					assert f'Playback launch: mode {3 if ghost else 6}, track NeoCity, laps 10, entrants {2 if ghost else 1}, recording {int(ghost)}' in log.read_text()
					assert read_cfg(cfg)['GameType'] == ('GhostMode' if ghost else 'TimeAttack')

					if ghost:
						recordable(process, log)

						if mode == 'ghost':
							before_restart = ticks(log)

							for button in ('Escape', 'Down', 'Return'):
								key(race, button, process, log)

							assert intro_ms(log) < 6000, diagnostic(log)
							assert ticks(log) >= before_restart, 'restart rewound lifetime slices'
							assert not list(recordings.glob('*.mde')), 'restart published discarded take'
							wait_frames(process, log, frames(log), 6)
							recordable(process, log)
					else:
						wait(process, log, lambda: intro_ms(log) > 20000)
						assert 'Frontend resumed:' not in log.read_text(), 'EOF ended demo session'
						assert not list(recordings.iterdir()) and not list(records.iterdir())

					if mode == 'close':
						close_window(race)
						assert process.wait(timeout=45) == 0, diagnostic(log)
						assert 'Playback return: completed 0, quit 1' in log.read_text()
					else:
						quit_race(process, log, race, ghost)
						assert window_for(process, log, 'main-menu preview') == menu

						if mode == 'ghost':
							assert not list(recordings.glob('*.mde')), 'take published before save decision'
							save_recording(process, log, menu, 'Ghost take')

							# Discover the just-published user take without restarting
							# the frontend, then race against its human and ghost.
							time.sleep(.6)

							for name in ('Return', 'Right', 'Right', 'Right', 'Right', 'Return',
								'Right', 'Down', 'Return', 'Return'):
								menu_key(menu, name)

							race = window_for(process, log, r'GHOST \| 3 cars')
							xdo('windowfocus', '--sync', race)
							wait_frames(process, log, frames(log), 6)
							close_window(race)
							assert process.wait(timeout=45) == 0, diagnostic(log)
							assert 'entrants 3, recording 1' in log.read_text()
							assert read_cfg(cfg)['DemoFileName'] == 'Ghost take'

				if mode != 'close':
					if process.poll() is None:
						close_window(menu)
						assert process.wait(timeout=45) == 0, diagnostic(log)

				if ghost and mode != 'close':
					takes = list(recordings.glob('*.mde'))
					assert len(takes) == 1, diagnostic(log)
					decoded = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(takes[0])], text=True, timeout=COMMAND_TIMEOUT)
					assert '"Ghost driver" "TEST" "'+car+'"' in decoded, decoded[:1500]
					assert '"JeePen" "SMOB" "Avalanche"' in decoded and 'validated ' in decoded
					assert struct.unpack_from('<III', takes[0].read_bytes(), 24) == (10, 2, 0)
				elif mode == 'close':
					assert not list(recordings.iterdir()), 'window close retained a recording'
				else:
					assert not list(recordings.iterdir()) and not list(records.iterdir())

				assert snapshot(demos) == before and not list(root.rglob('*.tmp.*'))

				if args.sound:
					for marker in ('Audio disabled:', 'Audio:', 'Message sound:', 'Effects:'):
						assert marker not in log.read_text(), diagnostic(log)

				print(f'{mode}: frontend launch, saved mode, recording policy and cleanup passed', flush=True)
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
	parser.add_argument('--mode', action='append', choices=('demo', 'ghost', 'close', 'invalid', 'demo-finish'))
	parser.add_argument('--sound', action='store_true', help='exercise race effects with the dummy audio device')
	args = parser.parse_args()

	for mode in args.mode or ('demo', 'ghost', 'close', 'invalid', 'demo-finish'):
		if mode == 'demo-finish':
			finish(args)
		else:
			run(args, mode)


if __name__ == '__main__':
	main()
