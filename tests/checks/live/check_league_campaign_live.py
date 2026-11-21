#!/usr/bin/env python3
"""Finish a physical League campaign, reloading saves and presenting seasons.

Run under xvfb-run. The native diagnostic automates human driving only;
season boards, videos, unlocks and menu returns use real application input.
An explicit output directory retains logs and can resume an interrupted run.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from tests.checks.live.check_menu_league_live import saved
from tests.checks.live.check_menu_league_session_live import frontend, launch
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window


from tests.checks.support.process import COMMAND_TIMEOUT

SCHEDULES = {3: (8, 1), 2: (8, 1, 3, 4), 1: (8, 1, 3, 4, 7, 6), 0: (5,)}
AWARDS = {3: {'Goldbridge', 'RedRock'}, 2: {'NeoCity', 'Atlantika'},
	1: {'RuhrStadt', 'Olympos', 'NolbyHills'}, 0: {'OkkunSpeedway'}}


def state(args, path):
	text = saved(args.build, path)
	entries = re.split(r'(?m)(?=^(?:Human|AI)Player\t)', text)[1:]
	assert len(entries) == 26
	human = next(i for i, entry in enumerate(entries) if entry.startswith('HumanPlayer'))
	stage = 0 if human < 2 else 1 if human < 10 else 2 if human < 18 else 3
	race = int(re.search(r'(?m)^RaceNumber\t(\d+)$', text)[1])

	return stage, race, human


def menu(args, root, stage, race, complete):
	path = root / 'Leagues/default.lgf'
	before = path.read_bytes()
	progress = root / 'Progress'
	previous = {p.name: p.read_bytes() for p in progress.iterdir()}
	label = f'menu-{stage}-{race}'

	with frontend(args, root, label, page='league' if complete else 'standings',
		progress=progress) as (process, log, window):
		if complete:
			launch(window)
			season = window_for(process, log, 'Motorhead season standings')
			outcome = 4 - stage
			assert f'League season: stage {stage}, outcome {outcome}' in log.read_text(), diagnostic(log)
			assert path.read_bytes() == before
			menu_key(season, 'Return')
			video = window_for(process, log, 'Motorhead movie')
			menu_key(video, 'Escape')
			marker = 'Frontend resumed: main' if stage == 0 else 'Frontend resumed: standings'
			wait(process, log, lambda: marker in log.read_text())
			assert window_for(process, log, 'main-menu preview') == window
			expected = set(previous) | {name.lower() + '.flg' for name in AWARDS[stage]}
			assert {p.name for p in progress.iterdir()} == expected

			for name, data in previous.items():
				assert (progress / name).read_bytes() == data

			for name in AWARDS[stage]:
				data = subprocess.check_output([str(build_executable(args.build, 'test-track-flag')), '--encode', name], timeout=COMMAND_TIMEOUT)
				assert (progress / (name.lower() + '.flg')).read_bytes() == data

			if stage:
				assert state(args, path)[:2] == (stage - 1, 0)
			else:
				assert 'League championship complete' in log.read_text()
		else:
			assert path.read_bytes() == before
			assert state(args, path)[:2] == (stage, race)

		close_window(window)
		assert process.wait(timeout=30) == 0, diagnostic(log)

	if not complete:
		assert path.read_bytes() == before
	elif stage == 0:
		# Retiring the final League creates a fresh Default when none remain.
		assert path.read_bytes() != before
		assert state(args, path)[:2] == (3, 0)
		assert set(re.findall(r'(?m)^Score\t(\d+)$', saved(args.build, path))) == {'0'}

	print(f'League stage {stage}, race {race}: saved menu reload' +
		('; season/video/unlocks and return passed' if complete else ' passed'), flush=True)


def run(args, root):
	for name in ('Leagues', 'Recordings', 'Progress', 'saves'):
		(root / name).mkdir(exist_ok=True)

	cfg = root / 'test.cfg'

	if not cfg.exists():
		cfg.write_bytes(encode(dict(PlayerName='Campaign driver', TeamName='TEST',
			CarName='Avalanche', TrackName='Goldbridge', GameType='League', LeagueName='Default',
			Difficulty='Easy', RecordRace='Off', MenuConfirmation='Off',
			SFXVolume='0', MusicVolume='0', ScreenDimension='640 400',
			Renderer='Software' if args.renderer == 'software' else 'Glide')))

	path = root / 'Leagues/default.lgf'

	for step in range(20):
		stage, race, human = state(args, path) if path.exists() else (3, 0, 18)
		count = len(SCHEDULES[stage])
		assert race <= count

		if race < count:
			if path.exists():
				(root / 'saves' / f'{stage}-{race}.LGF').write_bytes(path.read_bytes())

			log = root / f'race-{stage}-{race}.log'

			with log.open('w') as stream:
				result = subprocess.run([str(build_executable(args.build, 'mh-league-campaign')), str(args.gamedir),
					str(root / 'Leagues')], stdout=stream, stderr=stream, timeout=600)

			text = log.read_text()
			assert result.returncode == 0, text
			expected = f'Campaign race: stage {stage}, track {SCHEDULES[stage][race]},'
			assert expected in text, text
			print(text, end='', flush=True)
			new_stage, new_race, human = state(args, path)
			assert (new_stage, new_race) == (stage, race + 1)
			race = new_race

		complete = race == count

		if complete:
			# Promotion requires a top-two finish in the actual earned table.
			start = (0, 2, 10, 18)[stage]
			assert human < start + (1 if stage == 0 else 2), (stage, race, human)

		menu(args, root, stage, race, complete)

		if complete and stage == 0:
			assert len(list((root / 'Progress').glob('*.flg'))) == 8
			print('Complete patched League campaign: all divisions, physical final, durable reloads and championship menu return passed', flush=True)

			return

	raise AssertionError('campaign exceeded the bounded 13-race schedule')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('glide', 'software'), default='glide',
		help='rendering profile for a new settings file')
	parser.add_argument('--output', type=Path)
	args = parser.parse_args()

	if args.output:
		args.output.mkdir(parents=True, exist_ok=True)
		run(args, args.output)
	else:
		with tempfile.TemporaryDirectory(prefix='mh-league-campaign-') as directory:
			run(args, Path(directory))


if __name__ == '__main__':
	main()
