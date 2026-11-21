#!/usr/bin/env python3
"""Check retained League music across results, season, movies and failures.

Run under xvfb-run. Uses private CFG/LGF/progress and SDL dummy audio.
Music trace cursors count submitted source frames, not speaker latency.
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

from tests.checks.live.check_menu_league_live import saved
from tests.checks.live.check_menu_league_session_live import abort, accept, fixture, launch, race, write_fixture
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window


@contextmanager
def process_for(command, root, label):
	log = root/(label+'.log')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as output:
		process = subprocess.Popen(command + ['--debug-window-titles', '1',
			'--debug-camera-trace', '1',
			'--debug-music-trace', '1'], stdout=output, stderr=output, env=env)

		try:
			yield process, log
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


def command(args, root, movies=None, music=None):
	gamedir = args.gamedir
	replacements = {name: path for name, path in (('Movies', movies), ('Music', music))
		if path is not None}

	if replacements:
		gamedir = root / ('installation-' + '-'.join(replacements))
		prepare_view(gamedir, args.gamedir, replacements)

	return [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(gamedir), '--debug-settings', str(root/'test.cfg'),
		'--debug-leagues', str(root/'Leagues'), '--debug-recordings', str(root/'Recordings'),
		'--debug-progress', str(root/'Progress'),
		'--debug-no-records', '--debug-require-audio', '--debug-page', 'league', '--debug-window-titles', '1']


def menu(process, log):
	window = window_for(process, log, 'main-menu preview')
	time.sleep(.5)

	return window


def trace(log, continued):
	text = log.read_text(errors='replace')
	begins = re.findall(r'Music continuation begin: track (\d+), frame ([\d.]+)', text)
	ends = re.findall(r'Music continuation close: track (\d+), frame ([\d.]+), callbacks (\d+), failed (\d+)', text)
	assert len(begins) == len(ends) == 1, diagnostic(log)
	track = re.findall(r'Race music: CD track (\d+)', text)[-1] if continued else '2'
	assert begins[0][0] == ends[0][0] == track and ends[0][3] == '0'
	start, end = float(begins[0][1]), float(ends[0][1])
	assert start > 0 if continued else start == 0, (begins, ends)
	assert end > start+4410 and int(ends[0][2]) > 0, (begins, ends)
	assert ('Resumed season music:' in text) != continued, diagnostic(log)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-league-music-') as directory:
		root = Path(directory)

		for name in ('Leagues', 'Recordings', 'Progress'):
			(root/name).mkdir()

		cfg = dict(PlayerName='Music driver', TeamName='NOW',
			CarName=fields(args.gamedir/'Game/car01.car')['carname'],
			TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'],
			GameType='League', LeagueName='Default', RecordRace='Off', MenuConfirmation='Off',
			ViewDistance='40', SFXVolume='40', MusicVolume='80', MenuCDTrack='3')
		path = root/'Leagues/default.lgf'
		settings = root/'test.cfg'
		settings.write_bytes(encode(cfg))

		with process_for(command(args, root), root, 'create') as (process, log):
			close_window(menu(process, log))
			assert process.wait(timeout=30) == 0, diagnostic(log)

		original = saved(args.build, path)

		# Aborted League race -> results -> season retains the course song's
		# already advanced cursor, including while the world is unloaded.
		write_fixture(args.build, path, fixture(original, 18, 1))

		with process_for(command(args, root), root, 'continue') as (process, log):
			front = menu(process, log)
			launch(front)
			window = race(process, log)
			abort(process, log, window, path)
			assert 'Results music: CD track 2' not in log.read_text()
			time.sleep(.3)
			accept(process, log, window, path, 'Return')
			season = window_for(process, log, 'Motorhead season standings')
			time.sleep(.4)
			menu_key(season, 'Return')
			movie = window_for(process, log, 'Motorhead movie')
			trace(log, True)  # Music is already closed while the movie is open.
			assert 'Results music: CD track 2' not in log.read_text()
			menu_key(movie, 'Escape')
			wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
			assert log.read_text().count('Frontend music: CD track 3') == 2
			close_window(front)
			assert process.wait(timeout=30) == 0, diagnostic(log)

		print('League results retain the same song/cursor through season loading; CD stops before movie', flush=True)

		# Completed-save recovery is a native entry: no prior stream exists.
		# Close/error paths must release it and preserve the completed LGF.
		for phase in ('season-close', 'movie-close', 'movie-failure', 'missing-music', 'muted'):
			write_fixture(args.build, path, fixture(original, 18, 2))
			before = path.read_bytes()
			cfg['MusicVolume'] = '0' if phase == 'muted' else '80'
			settings.write_bytes(encode(cfg))
			cmd = command(args, root, movies=root/'absent-movies' if phase == 'movie-failure' else None,
				music=root/'absent-music' if phase == 'missing-music' else None)

			with process_for(cmd, root, phase) as (process, log):
				front = menu(process, log)
				launch(front)
				season = window_for(process, log, 'Motorhead season standings')
				time.sleep(.4)

				if phase in ('season-close', 'missing-music', 'muted'):
					close_window(season)
					assert process.wait(timeout=30) == 0, diagnostic(log)
				else:
					menu_key(season, 'Return')

					if phase == 'movie-close':
						movie = window_for(process, log, 'Motorhead movie')
						trace(log, False)
						close_window(movie)
						assert process.wait(timeout=30) == 0, diagnostic(log)
					else:
						wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
						assert 'Race: cannot enumerate asset directory' in log.read_text(), diagnostic(log)
						close_window(front)
						assert process.wait(timeout=30) == 0, diagnostic(log)

				assert path.read_bytes() == before, phase

				if phase in ('missing-music', 'muted'):
					assert 'Music continuation' not in log.read_text(), diagnostic(log)
				else:
					trace(log, False)

		print('Completed-save music recovery, muted/missing music, season/movie close and failure cleanup passed', flush=True)

		# Standalone League driver uses the same handoff, including its older
		# Enter/Escape season UI. Start on the last race and then close movie.
		write_fixture(args.build, path, fixture(original, 18, 1))
		cmd = [str(build_executable(args.build, 'openmotor')), '--debug-league', '--assets', str(args.gamedir), '--debug-save', str(path),
			'--debug-movies', str(args.gamedir/'Movies/movies.pak'), '--debug-no-settings', '--debug-no-records',
			'--debug-progress', str(root/'Progress'), '--debug-window-titles', '1']

		with process_for(cmd, root, 'standalone') as (process, log):
			window = race(process, log)
			abort(process, log, window, path)
			time.sleep(.3)
			accept(process, log, window, path, 'Return')
			season = window_for(process, log, 'Motorhead season standings')
			time.sleep(.4)
			menu_key(season, 'Return')
			movie = window_for(process, log, 'Motorhead movie')
			trace(log, True)
			close_window(movie)
			assert process.wait(timeout=30) == 0, diagnostic(log)

		print('Standalone League results/season music continuation and movie close passed', flush=True)


if __name__ == '__main__':
	main()
