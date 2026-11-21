#!/usr/bin/env python3
"""Check edited Personal names through new League, race recording and reload.

Run under xvfb-run. Uses isolated CFG/LGF/MDE storage and dummy audio.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import struct
import tempfile
import time

from tests.checks.live.check_legacy_names_live import write_cfg
from tests.checks.support.application import frontend
from tests.checks.live.check_menu_league_live import page, saved
from tests.checks.live.check_menu_league_session_live import launch, race, abort, accept
from tests.checks.support.catalog import fields
from tests.checks.support.menu import menu_key, menu_text, recordable, wait


def take(process, log, menu, path, name):
	window = race(process, log)
	recordable(process, log)
	abort(process, log, window, path)
	accept(process, log, window, path, 'Return')
	wait(process, log, lambda: 'Recording dialog: save' in log.read_text())
	time.sleep(.4)
	menu_key(menu, 'Return')
	wait(process, log, lambda: 'Recording name entry' in log.read_text())
	menu_text(menu, name)
	menu_key(menu, 'Return')
	wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())


def recorded_name(path):
	data = path.read_bytes()
	local = struct.unpack_from('<I', data, 32)[0]
	offset = 36 + 65 * local + 4

	return data[offset:offset + 16].split(b'\0')[0]


def run(args, root, name, normalized):
	leagues = root / 'Leagues'
	leagues.mkdir()
	cfg, log = root / 'test.cfg', root / 'live.log'
	values = dict(PlayerName=b'A', TeamName=b'NOW', RecordRace=b'On', MenuConfirmation=b'Off',
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'].encode('ascii'),
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'].encode('ascii'),
		GameType=b'League', LeagueName=b'Default', SFXVolume=b'0', MusicVolume=b'0',
		DetailMode=b'Custom', ViewDistance=b'40', ScreenDimension=b'640 400')
	write_cfg(cfg, values)
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-leagues', str(leagues), '--debug-recordings', str(root / 'Recordings'), '--debug-no-records', '--audio', '0',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	path = leagues / 'identity0000.lgf'

	with frontend(command, ['--debug-page', 'personal'], log, env) as (process, menu):
		menu_key(menu, 'BackSpace')

		for char in name.decode('ascii'):
			menu_key(menu, {' ': 'space', '/': 'slash', ';': 'semicolon'}.get(char, char))

		for button in ('Escape', 'Escape', 'Return', 'Right', 'Right'):
			menu_key(menu, button)

		page(process, log, menu, 'Return', 0)

		for _ in range(3):
			menu_key(menu, 'Right')

		page(process, log, menu, 'Return', 3)

		for _ in range(8):
			menu_key(menu, 'BackSpace')

		menu_text(menu, 'Identity')
		page(process, log, menu, 'Return', 0)
		raw = saved(args.build, path)
		assert ('HumanPlayer\t' + name.decode('ascii') + '\n') in raw, (
			name,
			[line for line in raw.splitlines() if line.startswith('HumanPlayer')],
		)
		initial = path.read_bytes()

		for _ in range(3):
			menu_key(menu, 'Left')

		launch(menu)
		take(process, log, menu, path, 'Before reload')

	assert recorded_name(root / 'Recordings/before reload.mde') == name
	assert ('HumanPlayer\t' + name.decode('ascii') + '\n') in saved(args.build, path)

	# Reuse the untouched first-race save so this identity check does not
	# enter season completion; that flow has its own live integration test.
	path.write_bytes(initial)

	# Existing League identity must come from its save, even when CFG changes.
	values.update(PlayerName=b'Different CFG', LeagueName=b'Identity')
	write_cfg(cfg, values)

	with frontend(command, ['--debug-page', 'league'], log, env) as (process, menu):
		launch(menu)
		take(process, log, menu, path, 'After reload')

	assert recorded_name(root / 'Recordings/after reload.mde') == normalized
	assert ('HumanPlayer\t' + normalized.decode('ascii') + '\n') in saved(args.build, path)
	assert not list(root.rglob('*.tmp.*'))
	print(f'Personal -> new League -> eight-car race/MDE -> reload: {name!r} -> {normalized!r}', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--case', action='append', choices=('empty', 'spaces', 'comment'))
	args = parser.parse_args()
	cases = dict(empty=(b'', b''), spaces=(b' A  B//C ', b'A B'), comment=(b'A;B', b'A'))

	for selected in args.case or cases:
		name, normalized = cases[selected]

		with tempfile.TemporaryDirectory(prefix='mh-league-identity-live-') as directory:
			run(args, Path(directory), name, normalized)


if __name__ == '__main__':
	main()
