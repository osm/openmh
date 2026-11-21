#!/usr/bin/env python3
"""Check original League load/save boundaries across real frontend sessions.

Run under xvfb-run. Edits private LGFs/CFG during Time Attack, then exercises
the refreshed League selection, result award and empty-directory fallback.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import tempfile

from tests.checks.live.check_league_abort_live import decode
from tests.checks.live.check_menu_controls_live import snapshot
from tests.checks.support.application import frontend
from tests.checks.live.check_menu_league_live import saved
from tests.checks.live.check_menu_league_session_live import abort, accept, race as league_race, write_fixture
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def attack(process, log):
	window = window_for(process, log, r'TIME ATTACK \| 1 cars')
	xdo('windowfocus', '--sync', window)
	wait_frames(process, log, frames(log), 6)

	return window


def leave(process, window, log):
	previous = log.read_text().count('Frontend resumed: main')
	key(window, 'Escape', process, log)
	key(window, 'Up', process, log)
	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: log.read_text().count('Frontend resumed: main') > previous)
	finally:
		xdo('keyup', 'Return')


def edited(text, name, score):
	text = text.replace('LeagueName\tDefault\n', f'LeagueName\t{name}\n', 1)
	pieces = re.split(r'(?m)(?=^(?:Human|AI)Player\t)', text)

	for i, piece in enumerate(pieces):
		if piece.startswith('HumanPlayer\t'):
			piece = re.sub(r'^HumanPlayer\t[^\n]+', 'HumanPlayer\tReload human', piece)
			pieces[i] = re.sub(r'(?m)^Score\t[^\n]+', f'Score\t{score}', piece)

	return ''.join(pieces).encode('latin1')


def run(args, root):
	cfg, log = root / 'test.cfg', root / 'live.log'
	leagues = root / 'userdata'
	default, added = leagues / 'default.lgf', leagues / '000Added.LGF'
	cfg.write_bytes(encode(dict(PlayerName='League reload', TeamName='TEST',
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'],
		GameType='TimeAttack', LeagueName='Defualt', RecordRace='Off', MenuConfirmation='Off',
		TrackLaps='1', SFXVolume='0', MusicVolume='0', DetailMode='Custom', ViewDistance='40',
		ScreenDimension='640 400')))
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
		'--debug-settings', str(cfg), '--debug-no-records', '--audio', '0', '--debug-recordings', str(root / 'Recordings'),
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with frontend(command, ['--debug-page', 'single'], log, env) as (process, menu):
		# Original entry creates an in-memory Default even outside League;
		# departure must save it before the first Time Attack race begins.
		assert leagues.is_dir() and not list(leagues.glob('*.lgf'))
		menu_key(menu, 'Return')
		window = attack(process, log)
		assert default.is_file() and read_cfg(cfg)['LeagueName'] == 'Default', diagnostic(log)
		original = saved(args.build, default)
		write_fixture(args.build, default, edited(original, 'Edited', 37))
		write_fixture(args.build, added, edited(original, 'Added', 9))
		values = read_cfg(cfg)
		values['LeagueName'] = 'Edited'
		cfg.write_bytes(encode(values))
		before_cfg, before_files = cfg.read_bytes(), snapshot(leagues)
		leave(process, window, log)
		assert cfg.read_bytes() == before_cfg and snapshot(leagues) == before_files

		# The freshly edited existing file is now index 1 after enumeration.
		# Its name must be selected through the newly loaded CFG.
		for button in ('Return', 'Right', 'Right', 'Return'):
			menu_key(menu, button)

		wait(process, log, lambda: 'League page: 0, count 2, selected 1' in log.read_text())

		for _ in range(3):
			menu_key(menu, 'Return')

		window = league_race(process, log)
		assert 'League launch: race 1/2' in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['LeagueName'] == 'Edited'
		header, human = decode(args.build, default)
		assert header['LeagueName'] == 'Edited' and human['Score'] == '37'
		abort(process, log, window, default)
		accept(process, log, window, default, 'Return')
		wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
		header, human = decode(args.build, default)
		assert header['RaceNumber'] == '1' and human['Score'] == '38', (header, human)
		assert human['HumanPlayer'] == 'Reload human'
		assert snapshot(leagues)[added.name] == before_files[added.name]
		print('League reload: non-League entry/departure, external LGF/CFG changes, fresh selection and real award persistence passed', flush=True)

		# Leave standings/setup, select Time Attack, and remove all saves
		# while that race is running. Reentry creates but does not save Default.
		for button in ('Escape', 'Escape', 'Right', 'Return', 'Return', 'Return'):
			menu_key(menu, button)

		window = attack(process, log)
		default.unlink()
		added.unlink()
		leave(process, window, log)
		assert not list(leagues.glob('*.lgf')), 'menu return prematurely saved the replacement Default'
		count = log.read_text().count('League page: 0, count 1, selected 0')

		for button in ('Return', 'Left', 'Return'):
			menu_key(menu, button)

		wait(process, log, lambda: log.read_text().count('League page: 0, count 1, selected 0') > count)

	assert {p.name for p in leagues.iterdir()} == {'default.lgf'}
	assert read_cfg(cfg)['LeagueName'] == 'Default'
	header, human = decode(args.build, default)
	assert header['LeagueName'] == 'Default' and header['RaceNumber'] == '0'
	assert human['HumanPlayer'] == 'League reload' and human['Score'] == '0'
	print('League reload: removed saves rebuild Default from current preferences, deferred save and clean shutdown passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	source = snapshot(args.gamedir / 'Game')

	with tempfile.TemporaryDirectory(prefix='mh-league-reentry-') as temporary:
		run(args, Path(temporary))

	assert snapshot(args.gamedir / 'Game') == source


if __name__ == '__main__':
	main()
