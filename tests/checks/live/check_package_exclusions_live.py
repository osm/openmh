#!/usr/bin/env python3
"""Verify a movie/music-free package reaches races and awards completed leagues."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.live.check_audio_toggle_live import race_audio
from tests.checks.live.check_menu_league_live import saved
from tests.checks.live.check_menu_league_session_live import frontend, fixture, launch, write_fixture
from tests.checks.support.build import build_executable
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window


def check_season(args, source, root):
	for name in ('Leagues', 'Recordings', 'Progress'):
		(root / name).mkdir()

	cfg = dict(PlayerName='No media driver', TeamName='NOW',
		CarName=fields(source / 'Game/car01.car')['carname'],
		TrackName=fields(source / 'Game/Track1.trk')['trackname'],
		GameType='League', LeagueName='Default', RecordRace='Off',
		MenuConfirmation='Off', MusicVolume='80', SFXVolume='80',
		Renderer='Software', ScreenDimension='640 400')
	(root / 'test.cfg').write_bytes(encode(cfg))
	path = root / 'Leagues/default.lgf'
	progress = root / 'Progress'

	with frontend(args, root, 'create', progress=progress) as (process, log, menu):
		close_window(menu)
		assert process.wait(timeout=30) == 0, diagnostic(log)

	original = saved(args.build, path)
	write_fixture(args.build, path, fixture(original, 18, 2))
	before = path.read_bytes()

	with frontend(args, root, 'season', progress=progress) as (process, log, menu):
		launch(menu)
		season = window_for(process, log, 'Motorhead season standings')
		menu_key(season, 'Return')
		wait(process, log, lambda: 'Frontend resumed: standings' in log.read_text())
		assert path.read_bytes() != before, 'season did not advance'
		assert {p.name for p in progress.iterdir()} == {'goldbridge.flg', 'redrock.flg'}
		assert 'Race:' not in log.read_text(), diagnostic(log)
		assert 'Movie playback:' not in log.read_text(), diagnostic(log)
		close_window(menu)
		assert process.wait(timeout=30) == 0, diagnostic(log)

	print('Absent league movie skips to awards, advances the save and returns to standings.', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	source = args.gamedir.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-exclusions-live-') as temporary:
		root = Path(temporary)
		package = root / 'minimal.ass'
		result = subprocess.run([str(build_executable(args.build, 'openmotorex')),
			'--exclude-music', '--exclude-movies', str(source), str(package)],
			capture_output=True, text=True, timeout=180)
		assert result.returncode == 0, result.stdout + result.stderr
		args.gamedir = package
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--assets', str(package),
			'--user-data', str(root / 'userdata'), '--renderer', 'software',
			'--debug-skip-loading-wait', '1', '--debug-no-records']
		race_audio(command, env, root, 1)
		text = (root / 'race-1.log').read_text()
		assert 'Opening movie:' not in text, text
		assert 'Race music: CD track' not in text, text
		check_season(args, source, root)

	print('Movie/music exclusions preserve menu startup, race audio and league progression.', flush=True)


if __name__ == '__main__':
	main()
