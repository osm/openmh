#!/usr/bin/env python3
"""Private X11 user-recording selection/deletion and source-preservation checks."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_ghost_preview import fixture, snapshot
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.live.check_menu_playback_live import quit_race
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import close_window, frames, wait_frames, xdo


def chord(window, *names):
	xdo('windowfocus', '--sync', window)
	xdo('keydown', *names)

	try:
		time.sleep(.09)
	finally:
		xdo('keyup', *names)

	time.sleep(.35)


def captures(args):
	with tempfile.TemporaryDirectory(prefix='mh-recording-captures-') as temporary:
		root = Path(temporary)
		game = root / 'game'
		demos = fixture(game, args.gamedir, ['Shared'])
		user = root / 'userdata'
		user.mkdir(parents=True)
		(user / 'Shared.mDe').write_bytes(b'private user filename fixture')
		cfg = root / 'private.cfg'
		cfg.write_text('CarHorn Honker1\nGameType GhostMode\nDemoFileName Shared\n')
		before = snapshot(demos), snapshot(user), cfg.read_bytes(), cfg.stat().st_mtime_ns

		for number, (directory, count, selected) in enumerate(((user, 2, 1), (None, 1, 0), (root / 'missing', 1, 0))):
			command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game), '--user-data', str(root / 'userdata'), '--debug-settings', str(cfg),
				'--debug-page', 'ghost', '--debug-selection', '2', '--audio', '0', '--debug-frames', '2',
				'--debug-capture', str(root / f'{number}.bmp'), '--debug-window-titles', '1']

			if directory is not None:
				command += ['--debug-recordings', str(directory)]

			result = subprocess.run(
				command,
				env=dict(os.environ, SDL_VIDEO_DRIVER='offscreen'),
				capture_output=True,
				text=True,
				timeout=60,
			)
			assert result.returncode == 0, result.stderr
			assert f'Demo catalog: count {count}, selected {selected}, name Shared' in result.stdout, result.stdout
			assert before == (snapshot(demos), snapshot(user), cfg.read_bytes(), cfg.stat().st_mtime_ns)
			assert not (root / 'missing').exists()

		print('3 captures: explicit user discovery, implicit-user isolation, missing-directory handling and source preservation passed', flush=True)


def run(args, case):
	with tempfile.TemporaryDirectory(prefix='mh-menu-recordings-') as temporary:
		root = Path(temporary)
		game = root / 'game'
		cfg = root / 'private.cfg'
		log = root / 'live.log'
		duplicate = case in ('launch', 'duplicate')
		installed = [] if case == 'off' else ['Same' if duplicate else 'Builtin']
		demos = fixture(game, args.gamedir, installed)
		before = snapshot(demos)
		user = root / 'userdata' if case == 'off' else root / 'recordings'
		names = ['Alpha', 'Beta', 'Gamma'] if case.startswith('confirm') else ['Same' if duplicate else 'User']

		if case == 'alias':
			user.symlink_to(demos, target_is_directory=True)
			names = []
		else:
			user.mkdir(parents=True)

			for name in names:
				(user / (name + '.mDe')).write_bytes(b'private user recording\n')

		if duplicate:
			original = (args.gamedir / 'Demos/JeePen.MDE').read_bytes()
			(user / 'Same.mDe').write_bytes(original[:4012 + 64 * 38])

		saved = 'Builtin' if case in ('alias', 'chord-remove') else names[0]
		confirmation = 2 if case == 'confirm2' else 1 if case in ('confirm1', 'replace') else 0
		confirmation_name = ('Off', 'On', 'NoExit')[confirmation]
		cfg.write_text(f'CarHorn Honker1\nGameType GhostMode\nDemoFileName {saved}\n'
			f'MenuConfirmation {confirmation_name}\nRecordRace Off\nSFXVolume 0\nMusicVolume 0\n')
		command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(game), '--user-data', str(root / 'userdata'), '--debug-settings', str(cfg),
			'--debug-no-records', '--audio', '0', '--debug-page', 'ghost', '--debug-selection',
			'0' if case.startswith('chord') else '2', '--debug-confirmation', str(confirmation),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		if case != 'off':
			command += ['--debug-recordings', str(user)]

		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				time.sleep(.6)

				if case.startswith('confirm'):
					menu_key(menu, 'Delete')
					menu_key(menu, 'Return')  # default NO!
					assert all((user / (n + '.mDe')).exists() for n in names)
					menu_key(menu, 'Delete')
					menu_key(menu, 'Escape')
					assert all((user / (n + '.mDe')).exists() for n in names)

					for _ in range(3):
						for name in ('Delete', 'Left', 'Return'):
							menu_key(menu, name)

					wait(process, log, lambda: not list(user.glob('*.mDe')))
					menu_key(menu, 'Delete')
					menu_key(menu, 'Escape')  # remaining installed entry
				elif case == 'off':
					menu_key(menu, 'Delete')
					wait(process, log, lambda: not list(user.glob('*.mDe')))
					menu_key(menu, 'Return')  # empty-list boundary; no launch
				elif case == 'chord-remove':
					chord(menu, 'Right', 'Down', 'Delete')
					wait(process, log, lambda: not list(user.glob('*.mDe')))
				elif case == 'chord-protect':
					chord(menu, 'Right', 'Up', 'Delete')
					menu_key(menu, 'Escape')
					assert (user / 'User.mDe').is_file()
				elif case == 'alias':
					menu_key(menu, 'Delete')
					menu_key(menu, 'Escape')
				elif case == 'replace':
					menu_key(menu, 'Delete')
					(user / 'User.mDe').rename(user / 'held.tmp')
					(user / 'User.mDe').write_bytes(b'changed while confirming')
					menu_key(menu, 'Left')
					menu_key(menu, 'Return')
					wait(process, log, lambda: 'Recording changed.' in log.read_text())
					menu_key(menu, 'Escape')
					assert (user / 'User.mDe').read_bytes() == b'changed while confirming'
					assert (user / 'held.tmp').read_bytes() == b'private user recording\n'
				elif case == 'launch':
					# Both roots contain Same; saved restoration selects the last
					# matching entry, and exact lookup must launch the valid user file.
					for name in ('Left', 'Down', 'Right', 'Return'):
						menu_key(menu, name)

					race = window_for(process, log, r'DEMO \| 1 cars')
					xdo('windowfocus', '--sync', race)
					wait_frames(process, log, frames(log), 6)
					quit_race(process, log, race)
					assert 'Playback: unsupported demo version' not in log.read_text()
				elif case == 'duplicate':
					# Explicitly choose the installed duplicate, visit Car setup,
					# then return. A catalog refresh must retain that exact entry.
					for name in ('Up', 'Return', 'Escape', 'Left', 'Down', 'Right', 'Return'):
						menu_key(menu, name)

					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())

					# The installed filename-only fixture has an invalid version word.
					assert 'Playback: unsupported demo version' in log.read_text(), diagnostic(log)
					menu_key(menu, 'Escape')

				close_window(menu)
				assert process.wait(timeout=45) == 0, diagnostic(log)

				# Original deletion chooses a neighbor even when file removal fails.
				expected = '' if case == 'off' else 'Same' if duplicate else 'Builtin'
				assert read_cfg(cfg).get('DemoFileName', '') == expected, (
					case,
					read_cfg(cfg).get('DemoFileName', ''),
				)

				if case == 'alias':
					assert 'Demo catalog: count 1,' in log.read_text(), diagnostic(log)

				if case == 'off':
					assert 'Demo catalog: count 0,' in log.read_text(), diagnostic(log)
					assert 'Ghost setup: focus 0,' in log.read_text(), diagnostic(log)

				assert snapshot(demos) == before, 'installed file bytes or mtime changed'
				assert not list(root.rglob('*.tmp.*'))
				print(f'{case}: user recording browser, delete routing and installed preservation passed', flush=True)
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
	parser.add_argument('--capture-only', action='store_true')
	parser.add_argument('--case', action='append', choices=('confirm1', 'confirm2', 'off', 'chord-remove',
		'chord-protect', 'alias', 'replace', 'launch', 'duplicate'))
	args = parser.parse_args()

	if args.capture_only or not args.case:
		captures(args)

	if args.capture_only:
		return

	for case in args.case or ('confirm1', 'confirm2', 'off', 'chord-remove', 'chord-protect', 'alias', 'replace', 'launch', 'duplicate'):
		run(args, case)


if __name__ == '__main__':
	main()
