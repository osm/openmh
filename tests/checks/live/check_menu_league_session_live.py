#!/usr/bin/env python3
"""Exercise the League frontend/race/season/recording handoff with real X11 input.

Run under xvfb-run. All CFG, LGF and MDE writes use temporary user storage.
Completed-season fixtures rearrange original saved profiles; no installed
resource is modified. They test presentation/lifecycle, not racing skill.
"""

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from tests.checks.live.check_league_abort_live import decode
from tests.checks.live.check_menu_league_live import saved
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, menu_text, recordable, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


@contextmanager

def frontend(args, root, label, page='league', movies=None, progress=None):
	log = root / (label + '.log')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	gamedir = args.gamedir

	if movies is not None:
		gamedir = root / (label + '-installation')
		prepare_view(gamedir, args.gamedir, {'Movies': movies})

	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(gamedir), '--debug-settings', str(root / 'test.cfg'),
		'--user-data', str(root / 'userdata'),
		'--debug-leagues', str(root / 'Leagues'), '--debug-recordings', str(root / 'Recordings'),
		'--debug-no-records', '--audio', '0', '--debug-page', page, '--debug-camera-trace', '1', '--debug-window-titles', '1']

	if progress is not None:
		command += ['--debug-progress', str(progress)]

	with log.open('w') as out:
		process = subprocess.Popen(command, stdout=out, stderr=out, env=env)

		try:
			window = window_for(process, log, 'main-menu preview')
			time.sleep(.5)
			yield process, log, window
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


def launch(menu, setup=True):
	for _ in range(3 if setup else 2):
		menu_key(menu, 'Return')


def race(process, log):
	window = window_for(process, log, r'\| 8 cars \|')
	xdo('windowfocus', '--sync', window)
	wait_frames(process, log, frames(log), 6)

	return window


def abort(process, log, window, path):
	before = path.read_bytes()

	for name in ('Escape', 'Up', 'Return'):
		key(window, name, process, log)

	wait_frames(process, log, frames(log), 3)
	assert 'Race results' in xdo('getwindowname', window), diagnostic(log)
	assert 'Pos 8/8' in xdo('getwindowname', window), diagnostic(log)
	assert path.read_bytes() == before, 'unaccepted results changed League'

	return before


def accept(process, log, window, path, name):
	before = path.read_bytes()
	xdo('keydown', name)

	try:
		wait(process, log, lambda: path.read_bytes() != before)
	finally:
		xdo('keyup', name)


def fixture(text, slot, races):
	text = re.sub(r'(?m)^//.*\n', '', text)
	pieces = re.split(r'(?m)(?=^(?:Human|AI)Player\t)', text)
	header, entries = pieces[0], pieces[1:]
	assert len(entries) == 26
	human = next(i for i, entry in enumerate(entries) if entry.startswith('HumanPlayer'))
	entries[human], entries[slot] = entries[slot], entries[human]
	header = re.sub(r'(?m)^RaceNumber\t\d+', f'RaceNumber\t{races}', header)

	return (header + ''.join(entries)).encode('latin1')


def write_fixture(build, path, text):
	path.write_bytes(subprocess.check_output([str(build_executable(build, 'test-car')), '--tbl-encode', path.name], input=text, timeout=COMMAND_TIMEOUT))


def close_unlocks(args, root, original):
	path = root / 'Leagues/default.lgf'
	progress = root / 'close-progress'
	progress.mkdir()
	write_fixture(args.build, path, fixture(original, 18, 2))
	before = path.read_bytes()

	for phase in ('season', 'movie'):
		with frontend(args, root, 'winning-close-' + phase, progress=progress) as (process, log, menu):
			launch(menu)
			window = window_for(process, log, 'Motorhead season standings')

			if phase == 'movie':
				menu_key(window, 'Return')
				window = window_for(process, log, 'Motorhead movie')

			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert path.read_bytes() == before and not list(progress.glob("*.flg")), 'close awarded a winning season'

	print('Closing a winning season/movie preserves the completed save and awards no markers', flush=True)


def cli_unlocks(args, root, original):
	path = root / 'Leagues/default.lgf'
	progress = root / 'cli-progress'
	progress.mkdir()
	write_fixture(args.build, path, fixture(original, 18, 2))
	log = root / 'cli-unlocks.log'
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as out:
		process = subprocess.Popen(
			[str(build_executable(args.build, 'openmotor')), '--debug-league', '--assets', str(args.gamedir),
				'--debug-save', str(path), '--debug-movies', str(args.gamedir / 'Movies/movies.pak'), '--debug-progress', str(progress),
				'--debug-skip-loading-wait', '1',
				'--debug-no-settings', '--debug-no-records', '--debug-mute', '--debug-window-titles', '1'],
			stdout=out,
			stderr=out,
			env=env,
		)

		try:
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Return')
			movie = window_for(process, log, '^Motorhead$')
			menu_key(movie, 'Escape')
			window = window_for(process, log, r'\| 8 cars \|')
			assert {p.name for p in progress.glob("*.flg")} == {'goldbridge.flg', 'redrock.flg'}
			assert decode(args.build, path)[0]['RaceNumber'] == '0'
			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			if process.poll() is None:
				process.terminate()
				process.wait(timeout=10)

	print('Standalone League launcher saves earned markers and starts the promoted season', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--race-only', action='store_true',
		help='check race/recording/standings handoff without season/unlock cases')
	parser.add_argument('--unlock-close-only', action='store_true')
	parser.add_argument('--cli-unlocks-only', action='store_true')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-menu-league-session-') as temporary:
		root = Path(temporary)
		(root / 'Leagues').mkdir()
		(root / 'Recordings').mkdir()
		path = root / 'Leagues/default.lgf'
		cfg = dict(PlayerName='League driver', TeamName='NOW',
			CarName=fields(args.gamedir / 'Game/car01.car')['carname'],
			TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'], GameType='League',
			LeagueName='Default', RecordRace='On', MenuConfirmation='Off', SFXVolume='0', MusicVolume='0')
		(root / 'test.cfg').write_bytes(encode(cfg))

		if args.unlock_close_only or args.cli_unlocks_only:
			with frontend(args, root, 'create') as (process, log, menu):
				close_window(menu)
				assert process.wait(timeout=30) == 0, diagnostic(log)

			if args.unlock_close_only:
				close_unlocks(args, root, saved(args.build, path))
			else:
				cli_unlocks(args, root, saved(args.build, path))

			return

		with frontend(args, root, 'race') as (process, log, menu):
			launch(menu)
			window = race(process, log)
			assert 'League launch: race 1/2' in log.read_text() and ', track 8,' in log.read_text(), diagnostic(log)
			original = saved(args.build, path)
			recordable(process, log)
			abort(process, log, window, path)
			accept(process, log, window, path, 'Return')
			wait(process, log, lambda: 'Recording dialog: save' in log.read_text())
			assert window_for(process, log, 'main-menu preview') == menu
			header, human = decode(args.build, path)
			assert header['RaceNumber'] == '1' and human['Score'] == '1', (header, human)
			time.sleep(.4)
			menu_key(menu, 'Return')
			wait(process, log, lambda: 'Recording name entry' in log.read_text())
			menu_text(menu, 'League take')
			menu_key(menu, 'Return')
			wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
			take = root / 'Recordings/league take.mde'
			info = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(take)], text=True, timeout=COMMAND_TIMEOUT)
			assert '"League driver" "NOW"' in info and 'validated ' in info, info

			# Same menu -> next race; closing unaccepted results awards nothing.
			launch(menu, setup=False)
			window = race(process, log)
			before = abort(process, log, window, path)
			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert path.read_bytes() == before, 'window close awarded results'

		print('League race: roster, abort award, MDE save, standings return and unaccepted close passed', flush=True)

		if args.race_only:
			return

		cfg['RecordRace'] = 'Off'
		(root / 'test.cfg').write_bytes(encode(cfg))

		with frontend(args, root, 'season-close') as (process, log, menu):
			launch(menu)
			window = race(process, log)
			abort(process, log, window, path)
			accept(process, log, window, path, 'Escape')
			season = window_for(process, log, 'Motorhead season standings')
			before = path.read_bytes()
			header, human = decode(args.build, path)
			assert header['RaceNumber'] == '2' and human['Score'] == '2', (header, human)
			close_window(season)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert path.read_bytes() == before, 'season close rolled over'

		with frontend(args, root, 'movie-close') as (process, log, menu):
			launch(menu)
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Escape')
			movie = window_for(process, log, 'Motorhead movie')
			close_window(movie)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert path.read_bytes() == before, 'movie close rolled over'

		# A malformed movie fails back to a usable completed standings page.
		(root / 'missing-movies').mkdir()
		(root / 'missing-movies/movies.pak').write_bytes(b'invalid movie collection')

		with frontend(args, root, 'movie-failure', movies=root / 'missing-movies') as (process, log, menu):
			launch(menu)
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Return')
			wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
			assert window_for(process, log, 'main-menu preview') == menu
			assert path.read_bytes() == before
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)

		print('League season: accepted final race, season/movie close, reload and movie-failure recovery passed', flush=True)
		close_unlocks(args, root, original)

		progress = root / 'userdata'
		earned = set()
		assert progress.is_dir() and not list(progress.glob("*.flg")), 'unaccepted/losing seasons wrote markers'
		awards = {1: ('Goldbridge', 'RedRock'), 2: ('NeoCity', 'Atlantika'),
			3: ('RuhrStadt', 'Olympos', 'NolbyHills'), 4: ('OkkunSpeedway',)}
		catalogs = {1: '03, tracks 4/6, cars 6/9', 2: '0f, tracks 6/6, cars 9/9',
			3: '7f, tracks 7/7, cars 9/9', 4: 'ff, tracks 8/8, cars 10/10'}

		# Use isolated completed-save fixtures to test every outcome. Division
		# 3/2/1/0 schedules contain 2/4/6/1 ordinary tracks in these assets.
		for stage, slot, count, outcome, destination in (
			(3, 18, 2, 1, 16), (2, 10, 4, 2, 8), (1, 2, 6, 3, 1),
			(3, 21, 2, 5, 21), (2, 16, 4, 6, 18), (0, 1, 1, 7, 2), (0, 0, 1, 4, None)):
			write_fixture(args.build, path, fixture(original, slot, count))

			if outcome == 4:
				other = root / 'Leagues/Other.LGF'
				write_fixture(args.build, other, original.replace('LeagueName\tDefault', 'LeagueName\tOther').encode('latin1'))
				other_before = other.read_bytes()

			with frontend(args, root, f'outcome{outcome}') as (process, log, menu):
				previous = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in progress.glob("*.flg")}
				launch(menu)
				season = window_for(process, log, 'Motorhead season standings')
				assert f'League season: stage {stage}, outcome {outcome}' in log.read_text(), diagnostic(log)
				menu_key(season, 'Return' if outcome % 2 else 'Escape')
				movie = window_for(process, log, 'Motorhead movie')
				menu_key(movie, 'Escape')
				marker = 'Frontend resumed: main' if outcome == 4 else 'Frontend resumed: standings'
				wait(process, log, lambda: marker in log.read_text())
				assert window_for(process, log, 'main-menu preview') == menu
				earned.update(awards.get(outcome, ()))
				assert {p.name for p in progress.glob("*.flg")} == {name.lower() + '.flg' for name in earned}

				for name in earned:
					expected = subprocess.check_output([str(build_executable(args.build, 'test-track-flag')), '--encode', name], timeout=COMMAND_TIMEOUT)
					assert (progress / (name.lower() + '.flg')).read_bytes() == expected

				for name, data in previous.items():
					p = progress / name
					assert (p.read_bytes(), p.stat().st_mtime_ns) == data, 'already available marker rewritten'

				if outcome in catalogs:
					assert 'Progress: availability ' + catalogs[outcome] in log.read_text(), diagnostic(log)

				if outcome == 4:
					assert not path.exists() and other.read_bytes() == other_before
				else:
					text = saved(args.build, path)
					entries = re.split(r'(?m)(?=^(?:Human|AI)Player\t)', text)[1:]
					assert entries[destination].startswith('HumanPlayer'), text
					assert re.search(r'(?m)^RaceNumber\t0$', text)
					assert all(int(value) == 0 for value in re.findall(r'(?m)^(?:Score|Wins|Seconds|Thirds)\t(\d+)$', text))

				close_window(menu)
				assert process.wait(timeout=30) == 0, diagnostic(log)

			print(f'League outcome {outcome}: season/movie, persistent transition, earned markers and menu return passed', flush=True)

		# Reject a marker link, preserve the completed season, then retry only
		# the missing marker. The other successful marker must not be rewritten.
		external = root / 'external-progress'
		external.mkdir()
		protected = root / 'protected'
		protected.write_bytes(b'preserve this file')
		blocked = external / 'goldbridge.flg'
		blocked.symlink_to(protected)
		cfg['LeagueName'] = 'Default'
		(root / 'test.cfg').write_bytes(encode(cfg))
		write_fixture(args.build, path, fixture(original, 18, 2))
		before = path.read_bytes()

		with frontend(args, root, 'write-failure', progress=external) as (process, log, menu):
			launch(menu)
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Return')
			movie = window_for(process, log, 'Motorhead movie')
			menu_key(movie, 'Escape')
			wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
			assert path.read_bytes() == before and protected.read_bytes() == b'preserve this file'
			successful = external / 'redrock.flg'
			first_time = successful.stat().st_mtime_ns
			blocked.unlink()
			menu_key(menu, 'Return')  # Dismiss the error requester.
			launch(menu, setup=False)
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Escape')
			movie = window_for(process, log, 'Motorhead movie')
			menu_key(movie, 'Return')
			wait(process, log, lambda: 'League season saved' in log.read_text())
			assert successful.stat().st_mtime_ns == first_time and blocked.is_file()
			assert decode(args.build, path)[0]['RaceNumber'] == '0'
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)

		print('League unlock write failure preserves the season; retry keeps successful markers unchanged', flush=True)

		# The cheat changes in-memory availability only, even after a win.
		empty = root / 'cheat-progress'
		empty.mkdir()
		cfg.update(PlayerName='R Pettersson', TeamName='SWE')
		(root / 'test.cfg').write_bytes(encode(cfg))
		write_fixture(args.build, path, fixture(original, 18, 2))

		with frontend(args, root, 'cheat', progress=empty) as (process, log, menu):
			launch(menu)
			season = window_for(process, log, 'Motorhead season standings')
			menu_key(season, 'Return')
			movie = window_for(process, log, 'Motorhead movie')
			menu_key(movie, 'Escape')
			wait(process, log, lambda: 'League season saved' in log.read_text())
			assert not list(empty.iterdir())
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			assert 'Catalog: availability ff, tracks 8/8, cars 10/10' in log.read_text(), diagnostic(log)

		print('League unlock cheat remains memory-only after a winning season', flush=True)
		cli_unlocks(args, root, original)
		assert not list(root.rglob('*.tmp.*'))


if __name__ == '__main__':
	main()
