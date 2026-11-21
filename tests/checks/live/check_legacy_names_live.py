#!/usr/bin/env python3
"""Check legacy name bytes across X11 menus, races, recordings and CFG reload.

Uses private CFG/LGF/MDE files and the original installed assets read-only.
Run under xvfb-run; a Swedish XKB layout supplies original single-byte input.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_league_live import saved
from tests.checks.live.check_menu_league_session_live import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, menu_text, recordable, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT

PLAYER = b'Bj\xf6rn \xc5kesson'
TEAM = b'\xc5\xc4\xd6'
TAKE = b'Take \xe5\xe4\xf6'
LEAGUE = b'\xc5ke'


def write_cfg(path, values):
	path.write_bytes(b''.join(k.encode('ascii')+b' '+v+b'\n' for k, v in values.items()))


def read_cfg(path):
	return dict(pair for line in path.read_bytes().split(b'\n')
		if len(pair := line.split(None, 1)) == 2 and not pair[0].startswith(b'//'))


def marker(log, value):
	return value.encode('ascii') in log.read_bytes()


def quit_race(process, log, race, message):
	for button in ('Escape', 'Up'):
		key(race, button, process, log)

	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: marker(log, message))
	finally:
		xdo('keyup', 'Return')


def close(process, log, menu):
	close_window(menu)
	assert process.wait(timeout=30) == 0, diagnostic(log)


def layout(name):
	subprocess.run(['setxkbmap', '-layout', name], check=True, capture_output=True, timeout=COMMAND_TIMEOUT)
	time.sleep(.3)


def run(args):
	with tempfile.TemporaryDirectory(prefix='mh-legacy-names-') as temporary:
		root = Path(temporary)
		(root/'Leagues').mkdir()
		(root/'Recordings').mkdir()
		cfg = root/'test.cfg'
		values = dict(PlayerName=PLAYER, TeamName=TEAM,
			CarName=fields(args.gamedir/'Game/car01.car')['carname'].encode('ascii'),
			TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'].encode('ascii'),
			CarHorn=b'Honker1', GameType=b'TimeAttack', RecordRace=b'On',
			MenuConfirmation=b'Off', ViewDistance=b'40', SFXVolume=b'0', MusicVolume=b'0')
		write_cfg(cfg, values)

		with frontend(args, root, 'record', page='car') as (process, log, menu):
			menu_key(menu, 'Return')
			race = window_for(process, log, r'TIME ATTACK \| 1 cars')
			xdo('windowfocus', '--sync', race)
			recordable(process, log)
			quit_race(process, log, race, 'Recording dialog: save')
			time.sleep(.4)
			menu_key(menu, 'Return')
			wait(process, log, lambda: marker(log, 'Recording name entry'))
			menu_text(menu, 'Take ')
			layout('se')

			try:
				for button in ('aring', 'adiaeresis', 'odiaeresis'):
					menu_key(menu, button)
			finally:
				layout('us')

			menu_key(menu, 'Return')
			wait(process, log, lambda: marker(log, 'Frontend resumed: main'))
			close(process, log, menu)

		take = root/'Recordings'/os.fsdecode(TAKE.lower()+b'.mde')
		original_take = take.read_bytes()
		assert original_take[40:56].split(b'\0')[0] == PLAYER
		assert original_take[56:61].split(b'\0')[0] == TEAM
		info = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(take)], timeout=COMMAND_TIMEOUT)
		assert b'validated ' in info
		assert read_cfg(cfg)[b'PlayerName'] == PLAYER and read_cfg(cfg)[b'TeamName'] == TEAM
		print('Time Attack: legacy player/team bytes survive CFG, capture and saved MDE', flush=True)

		# Select the new name through the chooser instead of pre-seeding it.
		values.update(GameType=b'GhostMode', DemoFileName=b'JeePen', RecordRace=b'Off')
		write_cfg(cfg, values)

		with frontend(args, root, 'select', page='ghost') as (process, log, menu):
			menu_key(menu, 'Right')
			menu_key(menu, 'Down')
			close(process, log, menu)

		assert read_cfg(cfg)[b'DemoFileName'] == TAKE, read_cfg(cfg)

		with frontend(args, root, 'replay', page='ghost') as (process, log, menu):
			for button in ('Down', 'Right', 'Return'):
				menu_key(menu, button)

			race = window_for(process, log, r'DEMO \| 1 cars')
			assert marker(log, 'Playback launch: mode 6, track RedRock'), diagnostic(log)
			wait_frames(process, log, frames(log), 6)
			quit_race(process, log, race, 'Frontend resumed: main')
			close(process, log, menu)
			assert b'name '+TAKE in log.read_bytes(), diagnostic(log)

		assert read_cfg(cfg)[b'DemoFileName'] == TAKE and take.read_bytes() == original_take
		print('Recording chooser: Swedish filename selection persists, reloads and plays', flush=True)

		values.update(GameType=b'League', LeagueName=b'Default')
		write_cfg(cfg, values)

		with frontend(args, root, 'league-new', page='league-new') as (process, log, menu):
			for _ in range(8):
				menu_key(menu, 'BackSpace')

			layout('se')

			try:
				menu_key(menu, 'shift+aring')
				menu_text(menu, 'ke')
			finally:
				layout('us')

			menu_key(menu, 'Return')
			menu_key(menu, 'Return')
			wait(process, log, lambda: marker(log, 'League page: 0,'))
			close(process, log, menu)

		assert read_cfg(cfg)[b'LeagueName'] == LEAGUE, read_cfg(cfg)
		documents = [saved(args.build, p).encode('latin1') for p in (root/'Leagues').glob('*.lgf')]
		assert any(b'LeagueName\t'+LEAGUE+b'\n' in text and b'HumanPlayer\t'+PLAYER+b'\n' in text
			for text in documents)

		with frontend(args, root, 'league-reload') as (process, log, menu):
			assert marker(log, 'League page: 0, count 2, selected 1'), diagnostic(log)
			close(process, log, menu)

		assert read_cfg(cfg)[b'LeagueName'] == LEAGUE
		assert not list(root.rglob('*.tmp.*'))
		print('League: Swedish name creation, LGF identity, CFG selection and reload passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	run(parser.parse_args())


if __name__ == '__main__':
	main()
