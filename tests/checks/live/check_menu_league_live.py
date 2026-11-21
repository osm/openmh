#!/usr/bin/env python3
"""Exercise League creation, selection, deletion and reload using X11 input.

Run under xvfb-run. All saves and CFG changes use a temporary user directory;
installed resources remain read-only. Race/session handoff is checked separately.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, menu_text, wait, window_for
from tests.checks.support.x11 import close_window


from tests.checks.support.process import COMMAND_TIMEOUT


def occurrences(log, marker):
	return log.read_text(errors='replace').count(marker)


def page(process, log, window, button, index):
	marker = f'League page: {index},'
	count = occurrences(log, marker)
	menu_key(window, button)
	wait(process, log, lambda: occurrences(log, marker) > count)
	time.sleep(.15)


def saved(build, file):
	return subprocess.check_output([str(build_executable(build, 'mh-tbl-decode')), str(file), '--key', file.name], timeout=COMMAND_TIMEOUT).decode('latin1')


def run(args):
	with tempfile.TemporaryDirectory(prefix='mh-menu-league-live-') as temporary:
		root = Path(temporary)
		cfg = root / 'private.cfg'
		leagues = root / 'Leagues'
		leagues.mkdir()
		cfg.write_bytes(encode(dict(PlayerName='League driver', TeamName='TEST',
			CarName=fields(args.gamedir / 'Game/car01.car')['carname'],
			TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'], GameType='League',
			LeagueName='Defualt', MenuConfirmation='On', SFXVolume='0', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		for cycle in range(4):
			log = root / f'live{cycle}.log'
			command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
				'--debug-leagues', str(leagues), '--debug-no-records', '--audio', '0', '--debug-page', 'main' if cycle == 3 else 'league',
				'--debug-window-titles', '1']

			with log.open('w') as output:
				process = subprocess.Popen(command, stdout=output, stderr=output, env=env)

				try:
					window = window_for(process, log, 'main-menu preview')
					time.sleep(.4)

					if cycle == 0:
						assert not list(leagues.iterdir())

						for _ in range(3):
							menu_key(window, 'Right')

						page(process, log, window, 'Return', 3)

						for _ in range(8):
							menu_key(window, 'BackSpace')

						menu_text(window, 'Friday')
						menu_key(window, 'Return')  # Original create question, initial Yes.
						page(process, log, window, 'Return', 0)
						assert {p.name for p in leagues.iterdir()} == {'friday0000.lgf', 'default.lgf'}
						assert 'LeagueName\tFriday\n' in saved(args.build, leagues / 'friday0000.lgf')
						assert 'HumanPlayer\tLeague driver\n' in saved(args.build, leagues / 'friday0000.lgf')

						# Escape on the question discards an already constructed draft.
						page(process, log, window, 'Return', 3)
						menu_key(window, 'Escape')
						page(process, log, window, 'Escape', 0)
						assert occurrences(log, 'League removed: count 2, selected 0') == 1, diagnostic(log)
						assert len(list(leagues.iterdir())) == 2
						menu_key(window, 'Left')
						page(process, log, window, 'Return', 2)
						menu_key(window, 'Down')  # Select Default; Escape must keep selection.
						page(process, log, window, 'Escape', 0)
						menu_key(window, 'Left')
						page(process, log, window, 'Return', 1)  # View stats.

						if args.capture_dir is not None:
							args.capture_dir.mkdir(parents=True, exist_ok=True)
							subprocess.run(
								['xwd', '-id', str(window), '-silent', '-out',
									str(args.capture_dir / 'league-standings.xwd')],
								check=True,
								timeout=COMMAND_TIMEOUT)

						page(process, log, window, 'Escape', 0)
						menu_key(window, 'Left')
						page(process, log, window, 'Return', 1)  # Continue -> standings -> car setup.
						menu_key(window, 'Return')
						page(process, log, window, 'Escape', 0)
					elif cycle == 1:
						assert 'League page: 0, count 2, selected 0' in log.read_text(), diagnostic(log)

						for _ in range(2):
							menu_key(window, 'Right')

						page(process, log, window, 'Return', 2)
						menu_key(window, 'Down')  # Sorted reload has Friday second.
						page(process, log, window, 'Escape', 0)

						for _ in range(2):
							menu_key(window, 'Right')

						page(process, log, window, 'Return', 4)
						menu_key(window, 'Return')
						menu_key(window, 'Return')  # Initial Keep it!
						assert occurrences(log, 'League removed:') == 0 and len(list(leagues.iterdir())) == 2

						for index in range(2):
							count = occurrences(log, 'League removed:')
							menu_key(window, 'Return')
							menu_key(window, 'Left')
							menu_key(window, 'Return')
							wait(process, log, lambda: occurrences(log, 'League removed:') > count)
							assert len(list(leagues.iterdir())) == 1 - index

						assert occurrences(log, 'League removed: count 1, selected 0') == 2
					elif cycle == 2:
						before = (leagues / 'default.lgf').read_bytes()

						for _ in range(3):
							menu_key(window, 'Right')

						page(process, log, window, 'Return', 3)

						# Window close never silently accepts the unconfirmed draft.
					else:
						menu_key(window, 'Return')  # Main -> Single player -> League.
						menu_key(window, 'Right')
						menu_key(window, 'Right')
						page(process, log, window, 'Return', 0)
						menu_key(window, 'Escape')
						menu_key(window, 'Escape')

					close_window(window)
					assert process.wait(timeout=20) == 0, diagnostic(log)
				finally:
					if process.poll() is None:
						process.terminate()
						process.wait(timeout=10)

			assert read_cfg(cfg)['LeagueName'] == 'Default'

			if cycle == 2:
				assert (leagues / 'default.lgf').read_bytes() == before

		assert {p.name for p in leagues.iterdir()} == {'default.lgf'}
		text = saved(args.build, leagues / 'default.lgf')
		assert 'HumanPlayer\tLeague driver\n' in text and 'CreateDate\t0\n' not in text

	print('League X11 flow: all five pages, create/keep/discard, selection, car return, '
		'confirmation defaults, last-delete replacement and CFG/save reload passed')


def main():
	p = argparse.ArgumentParser(description=__doc__)
	p.add_argument('--gamedir', type=Path, required=True)
	p.add_argument('--build', type=Path, default=Path('build/linux'))
	p.add_argument('--capture-dir', type=Path)
	run(p.parse_args())


if __name__ == '__main__':
	main()
