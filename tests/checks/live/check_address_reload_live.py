#!/usr/bin/env python3
"""Check NPC edits/additions/removals across frontend race returns under X11.

No sockets are needed: real Join-page rendering observes the reloaded names
and IP strings. All writes use private files; installed assets stay intact.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.live.check_league_reload_live import attack, leave
from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.installation import prepare_view
from tests.checks.support.menu import diagnostic, menu_key, wait
from tests.checks.support.settings import encode


def npc(path, name, address):
	path.write_text(f'Type TCP\nDescription {name}\nAddress {address}\n')


def snapshot(directory):
	if not directory.exists():
		return None

	return {p.name: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ino)
		for p in directory.iterdir() if p.is_file() and p.suffix.lower() == ".npc"}


def select(process, menu, log, index, count, name, address):
	start = log.stat().st_size
	menu_key(menu, 'Home')

	for _ in range(index):
		menu_key(menu, 'Down')

	expected = f'Address selection: {index}/{count}, name {name}, IP {address}\n'
	wait(process, log, lambda: expected in log.read_text()[start:])


def run(args, root, explicit):
	root.mkdir()
	game = root/'game'
	prepare_view(game, args.gamedir)
	book = root/'addresses' if explicit else game/'userdata'
	book.mkdir(parents=True)
	old, added = book/'Old.npc', book/'Zed.npc'
	npc(old, 'Before race', 'before.example')
	cfg, log = root/'private.cfg', root/'live.log'
	cfg.write_bytes(encode(dict(PlayerName='NPC reload', TeamName='TEST',
		CarName=fields(args.gamedir/'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'], GameType='TimeAttack',
		TrackLaps='1', RecordRace='Off', MenuConfirmation='Off', CarHorn='Honker1',
		SFXVolume='0', MusicVolume='0', DetailMode='Custom', ViewDistance='40', ScreenDimension='640 400')))
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game),
		'--debug-settings', str(cfg), '--audio', '0', '--debug-no-records', '--debug-recordings', str(root/'recordings'),
		'--debug-camera-trace', '1',
		'--debug-address-trace', '1',
		'--debug-window-titles', '1']

	if explicit:
		command += ['--debug-addresses', str(book)]

	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with frontend(command, [], log, env) as (process, menu):
		for button in ('Right', 'Return', 'Return'):
			menu_key(menu, button)

		select(process, menu, log, 0, 1, 'Before race', 'before.example')

		for button in ('Escape', 'Escape', 'Left', 'Return', 'Return'):
			menu_key(menu, button)

		window = attack(process, log)
		npc(old, 'Changed during race', 'changed.example')
		npc(added, 'Added during race', 'added.example')
		before = snapshot(book)
		leave(process, window, log)
		assert snapshot(book) == before, 'reentry wrote NPC files'
		start = log.stat().st_size

		for button in ('Right', 'Return', 'Return'):
			menu_key(menu, button)

		wait(process, log, lambda: 'Address selection: 1/2, name Changed during race, IP changed.example\n' in log.read_text()[start:])
		select(process, menu, log, 0, 2, 'Added during race', 'added.example')
		select(process, menu, log, 1, 2, 'Changed during race', 'changed.example')

		# Insert a name before the selection; persistence must sort the row
		# and carry its deletion identity without selecting a different host.
		start = log.stat().st_size
		menu_key(menu, 'Insert')

		for _ in 'Server 03':
			menu_key(menu, 'BackSpace')

		for char in 'Aardvark':
			menu_key(menu, char)

		menu_key(menu, 'Down')

		for char in 'manual.example':
			menu_key(menu, 'period' if char == '.' else char)

		menu_key(menu, 'Return')
		wait(process, log, lambda: 'Address selection: 2/3, name Changed during race, IP changed.example\n' in log.read_text()[start:])
		manual = book/'Aardvark.NPC'
		assert manual.exists() and b'manual.example' in manual.read_bytes()
		select(process, menu, log, 0, 3, 'Aardvark', 'manual.example')
		start = log.stat().st_size
		menu_key(menu, 'Delete')
		wait(process, log, lambda: 'Multiplayer address deleted: 2 entries' in log.read_text()[start:])
		assert snapshot(book) == before, 'sorted deletion touched another NPC'
		select(process, menu, log, 1, 2, 'Changed during race', 'changed.example')

		# Retain the old file across a description sort, then remove both.
		# Multi entry must retain that index until Join drawing clamps it.
		for button in ('Escape', 'Escape', 'Left', 'Return', 'Return'):
			menu_key(menu, button)

		window = attack(process, log)
		old.unlink()
		added.unlink()
		leave(process, window, log)
		start = log.stat().st_size

		for button in ('Right', 'Return', 'Return'):
			menu_key(menu, button)

		wait(process, log, lambda: 'Address selection: empty\n' in log.read_text()[start:])
		assert not snapshot(book), diagnostic(log)

		# The final deletion must leave an empty menu that can add again.
		menu_key(menu, 'Insert')
		menu_key(menu, 'Up')
		menu_key(menu, 'Down')

		for _ in 'manual.example':
			menu_key(menu, 'BackSpace')

		for char in '127.0.0.1':
			menu_key(menu, 'period' if char == '.' else char)

		menu_key(menu, 'Return')
		select(process, menu, log, 0, 1, 'Server 01', '127.0.0.1')
		assert (book/'server 01.npc').exists()
		start = log.stat().st_size
		menu_key(menu, 'Delete')
		wait(process, log, lambda: 'Multiplayer address deleted: 0 entries' in log.read_text()[start:])
		assert not snapshot(book), diagnostic(log)

		for button in ('Return', 'Delete', 'Down', 'End'):
			menu_key(menu, button)

		assert process.poll() is None, diagnostic(log)

	assert not snapshot(book), 'shutdown rewrote removed NPCs'
	print(f'{"explicit" if explicit else "default"} NPC directory: description order, selected-file retention, sorted add/delete, removed-row clamp, two race returns and no reload writes passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	before = snapshot(args.gamedir/'AddressBook')

	with tempfile.TemporaryDirectory(prefix='mh-address-reentry-') as temporary:
		root = Path(temporary)

		for explicit in (False, True):
			run(args, root/str(explicit), explicit)

	assert snapshot(args.gamedir/'AddressBook') == before


if __name__ == '__main__':
	main()
