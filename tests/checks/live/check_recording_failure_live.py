#!/usr/bin/env python3
"""Force a recording-specific stdio failure during X11 frontend races.

The test-only interposer limits writes to private recording streams. Native
asset tests separately exercise real short writes using RLIMIT_FSIZE, which
cannot safely limit a live GPU process's unrelated mapped temporary files.
Original assets and CFG remain read-only; all saves use a private directory.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_ghost_preview import fixture, snapshot
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, intro_ms, menu_key, recordable, ticks, wait, window_for
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def run(args, ghost, finalize=False):
	label = 'Ghost' if ghost else 'Time Attack'

	with tempfile.TemporaryDirectory(prefix='mh-recording-failure-live-') as temporary:
		root = Path(temporary)
		cfg = root / 'private.cfg'
		log = root / 'live.log'
		game = root / 'game'
		demos = fixture(game, args.gamedir, ['Private'])
		(demos / 'Private.MDE').write_bytes((args.gamedir / 'Demos/JeePen.MDE').read_bytes())
		before = snapshot(demos)
		cfg.write_bytes(encode(dict(PlayerName='Write failure', TeamName='TEST',
			CarName=fields(args.gamedir / 'Game/car02.car')['carname'],
			TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'],
			GameType='GhostMode' if ghost else 'TimeAttack', DemoFileName='Private',
			RecordRace='On', MenuConfirmation='Off', ViewDistance='40', SFXVolume='37', MusicVolume='0')))
		recordings = root / 'recordings'
		recordings.mkdir()
		shim = root / 'recording-fault.so'
		subprocess.run(
			['cc', '-std=c90', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
				'-fPIC', '-shared', 'tests/support/recording_fault_stdio.c', '-ldl', '-o', str(shim)],
			check=True,
			timeout=COMMAND_TIMEOUT)
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy', LD_PRELOAD=str(shim), MH_TEST_RECORDING_DIR=str(recordings))

		if finalize:
			env['MH_TEST_RECORDING_FINALIZE'] = '1'

		command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game), '--debug-settings', str(cfg),
			'--debug-no-records', '--debug-recordings', str(recordings), '--audio', '0',
			'--debug-page', 'ghost' if ghost else 'car', '--debug-camera-trace', '1', '--debug-window-titles', '1']
		stream = log.open('w')
		process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.6)

			for name in (('Right', 'Return', 'Return') if ghost else ('Return',)):
				menu_key(menu, name)

			race = window_for(process, log, r'GHOST \| 2 cars' if ghost else r'TIME ATTACK \| 1 cars')
			xdo('windowfocus', '--sync', race)
			marker = 'Recording stopped: cannot append recording block'

			if finalize:
				recordable(process, log)
				assert marker not in log.read_text() and len(list(recordings.glob('*.tmp.*'))) == 1
			else:
				wait(process, log, lambda: marker in log.read_text())
				failed_tick = ticks(log)
				wait(process, log, lambda: ticks(log) > failed_tick + 120)
				assert not list(recordings.iterdir()), 'failed take or temporary file was retained'
				assert 'Frontend resumed:' not in log.read_text(), diagnostic(log)

				for button in ('Escape', 'Down', 'Return'):
					key(race, button, process, log)

				wait_frames(process, log, frames(log), 6)
				wait(process, log, lambda: intro_ms(log) > 14000)
				assert log.read_text().count(marker) == 1 and not list(recordings.iterdir()), diagnostic(log)

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			# A short append disables recording through menu restart, but frontend
			# reentry reloads its On preference; only five AV words survive.
			expected = 'On'

			if finalize:
				assert ('Playback' if ghost else 'Race') + ': cannot finalize recording file' in log.read_text()
				assert 'Recording dialog: save' not in log.read_text() and 'Recording saved:' not in log.read_text()
				menu_key(menu, 'Return')  # Acknowledge the recoverable native error.
			else:
				assert ('Playback' if ghost else 'Race') + ' return: completed 1, quit 0' in log.read_text()

			assert read_cfg(cfg)['RecordRace'] == expected, read_cfg(cfg)
			assert window_for(process, log, 'main-menu preview') == menu
			close_window(menu)
			assert process.wait(timeout=45) == 0, diagnostic(log)
			assert read_cfg(cfg)['RecordRace'] == expected, 'frontend did not restore the saved recording choice'
			assert snapshot(demos) == before and not list(recordings.iterdir())
			assert not list(root.rglob('*.tmp.*'))

			if finalize:
				print(f'{label}: finalization failure, error notice, menu return and private-take cleanup passed', flush=True)
			else:
				print(f'{label}: write failure, recording disabled through menu restart, CFG On restored on menu return and cleanup passed', flush=True)
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

			stream.close()


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	for ghost in (False, True):
		run(args, ghost)
		run(args, ghost, True)


if __name__ == '__main__':
	main()
