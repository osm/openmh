#!/usr/bin/env python3
"""Check real race awards, X11 restart/exit boundaries and frontend Rankings.

Use xvfb-run, installed assets and isolated scores/CFG. Completed times come
from an actual eight-car AI race. Slow seeded Best Lap tables also expose the
original 1000000-ms reset sentinel on short aborted attempts.
"""

from tests.checks.support.build import build_executable
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_credits_live import capture
from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def read(args, directory, kind, track='RedRock', laps=3):
	data = subprocess.check_output(
		[str(build_executable(args.build, 'test-drive-scores')), '--read', str(directory),
			str(kind), str(laps), '1', '1', track],
		timeout=COMMAND_TIMEOUT)
	assert len(data) == 560

	return [struct.unpack_from('<I24s16s3I', data, i * 56) for i in range(10)]


def seed(directory, track='RedRock'):
	directory.mkdir(exist_ok=True)
	path = directory / f'blmm00{track.lower()}.mhi'
	path.write_bytes(b''.join(struct.pack('<I24s16s3I', 0, f'Default {i}'.encode(), b'Kobra X3',
		0, 2700000, 0) for i in range(10)))

	return path


def count(rows, name):
	return sum(row[1].split(b'\0')[0] == name.encode() for row in rows)


@contextmanager
def driving(command, log, env, pattern=r'\| 1 cars \|'):
	with log.open('w') as stream:
		process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

		try:
			window = window_for(process, log, pattern)
			xdo('windowfocus', '--sync', window)
			wait_frames(process, log, frames(log), 5)
			yield process, window
			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
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


def run(args, root):
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	completed = root / 'completed'
	completed.mkdir(exist_ok=True)
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir), '--debug-car', '1', '--debug-track', '2',
		'--debug-ai', '2', '--debug-laps', '1', '--debug-ticks', '18000', '--debug-opponents', '7', '--debug-check',
		'--debug-name', 'Score driver', '--debug-records', str(completed),
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
	(root / 'completed.log').write_text(result.stdout + result.stderr)
	assert result.returncode == 0 and 'Race results (8/8 finished)' in result.stdout, result
	row = next(line for line in result.stdout.splitlines() if re.match(r'\s*\d+\*', line))
	times = re.findall(r'(\d+):(\d+)\.(\d+)', row)
	total, best = [int(m) * 60000 + int(s) * 1000 + int(ms) for m, s, ms in times]
	track = fields(args.gamedir / 'Game/Track2.trk')['trackname']

	for kind in range(3):
		rows = read(args, completed, kind, track, 1)
		assert rows[0][0] == 1 and rows[0][1].split(b'\0')[0] == b'Score driver'
		assert rows[0][2].split(b'\0')[0] == b'Kobra X3' and rows[0][3:] == (0, best, total), rows[0]
		assert count(rows, 'Score driver') == 1

	print('Completed eight-car race: all three MHI tables contain the actual local time/name/car/transmission', flush=True)

	restarts = root / 'restarts'
	path = seed(restarts)
	before = path.read_bytes()
	log = root / 'restarts.log'
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir), '--debug-car', '1', '--debug-track', '1',
		'--debug-laps', '3', '--debug-ticks', '0', '--debug-opponents', '0', '--debug-name', 'Restart driver', '--debug-mute',
		'--debug-no-settings', '--debug-records', str(restarts), '--debug-camera-trace', '1', '--debug-window-titles', '1']

	with driving(command, log, env) as (process, window):
		key(window, 'r', process, log)
		assert path.read_bytes() == before, 'unbound R submitted a score'

		for expected in (1, 2):
			for button in ('Escape', 'Down', 'Return'):
				key(window, button, process, log)

			wait(process, log, lambda: count(read(args, restarts, 0), 'Restart driver') == expected)
			wait_frames(process, log, frames(log), 6)
			assert count(read(args, restarts, 0), 'Restart driver') == expected

	rows = read(args, restarts, 0)
	assert count(rows, 'Restart driver') == 3 and all(row[4:] == (1000000, 0) for row in rows[:3]), rows[:4]
	assert sorted(p.name for p in restarts.iterdir()) == ['blmm00redrock.mhi']
	print('Standalone X11: unbound R does not award; two menu restarts and window exit each award exactly once', flush=True)

	for mode in (0, 2):
		directory = root / f'menu-{mode}'
		score = seed(directory)
		log = root / f'menu-{mode}.log'
		cfg = root / f'menu-{mode}.cfg'
		cfg.write_bytes(encode(dict(PlayerName='Menu score', CarName='Kobra X3', TrackName='RedRock',
			GameType='Single' if mode == 0 else 'TimeAttack', TrackLaps='3', RecordRace='Off',
			MenuConfirmation='Off', CarTransmission='Auto', SFXVolume='0', MusicVolume='0',
			DetailMode='Custom', ScreenDimension='640 400')))
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
			'--audio', '0', '--debug-records', str(directory), '--debug-recordings', str(root / 'recordings'),
			'--debug-controls', str(root / 'controls'), '--debug-addresses', str(root / 'addresses'),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		for name in ('controls', 'addresses'):
			(root / name).mkdir(exist_ok=True)

		with frontend(command, ['--debug-page', 'car', '--debug-mode', str(mode), '--debug-record', '0', '--debug-confirmation', '0'], log, env) as (process, menu):
			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| [18] cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 5)
			expected = 1

			if mode == 2:
				for name in ('Escape', 'Down', 'Return'):
					key(race, name, process, log)

				wait_frames(process, log, frames(log), 6)
				assert count(read(args, directory, 0), 'Menu score') == 1
				expected = 2

			if mode == 0:
				good = score.read_bytes()
				score.write_bytes(b'TBL\x09broken')

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				if mode == 0:
					wait(process, log, lambda: 'Save results: unsupported TBL resource header' in log.read_text())
				else:
					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			if mode == 0:
				wait_frames(process, log, frames(log), 3)
				assert 'Frontend resumed: main' not in log.read_text()
				assert score.read_bytes() == b'TBL\x09broken'
				score.write_bytes(good)
				xdo('keydown', 'Return')

				try:
					wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
				finally:
					xdo('keyup', 'Return')

			wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			rows = read(args, directory, 0)
			assert count(rows, 'Menu score') == expected and rows[0][4:] == (1000000, 0), rows[:2]
			saved = score.read_bytes()

			for name in ('Right', 'Right', 'Return'):
				menu_key(menu, name)

			wait(process, log, lambda: 'Rankings: entry 1' in log.read_text())

			for _ in range(2 if mode == 0 else 1):
				menu_key(menu, 'Up')

			wait(process, log, lambda: 'file BLMM00RedRock.MHI, present 1' in log.read_text())
			time.sleep(.4)
			capture(menu, root / f'rankings-{mode}.png')
			assert score.read_bytes() == saved
			menu_key(menu, 'Escape')

		assert count(read(args, directory, 0), 'Menu score') == expected
		assert sorted(p.name for p in directory.iterdir()) == ['blmm00redrock.mhi']
		print(f'Frontend mode {mode}: {expected} restart/exit boundaries each award once; Rankings loads the new Best Lap', flush=True)

	assert not list(root.rglob('*.tmp.*'))


def launchers(args, root):
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	directory = root / 'ghost-cli'
	score = seed(directory, 'NeoCity')
	command = [str(build_executable(args.build, 'openmotor')), '--debug-demo', str(args.gamedir / 'Demos/JeePen.MDE'),
		'--assets', str(args.gamedir), '--debug-name', 'Ghost score', '--debug-ticks', '0', '--debug-check', '--debug-no-settings',
		'--debug-records', str(directory), '--debug-camera-trace', '1', '--debug-window-titles', '1']
	before = score.read_bytes()
	result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
	(root / 'demo-cli.log').write_text(result.stdout + result.stderr)
	assert result.returncode == 0 and score.read_bytes() == before, result
	result = subprocess.run(command + ['--debug-ghost'], env=env, capture_output=True, text=True, timeout=60)
	(root / 'ghost-cli.log').write_text(result.stdout + result.stderr)
	assert result.returncode == 0, result
	rows = read(args, directory, 0, 'NeoCity')
	assert count(rows, 'Ghost score') == 1 and rows[0][4:] == (1000000, 0), rows[:2]
	assert len(list(directory.iterdir())) == 1
	print('Standalone Demo/Ghost: playback leaves scores untouched; explicit Ghost storage awards only the local entrant', flush=True)

	save = root / 'ScoreLeague.LGF'
	log = root / 'league-cli.log'
	command = [str(build_executable(args.build, 'openmotor')), '--debug-league', '--assets', str(args.gamedir), '--debug-save', str(save),
		'--debug-new', 'Score league', '--debug-player', 'League score', '--debug-no-settings', '--debug-mute',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	result = subprocess.run(command + ['--debug-check'], env=env, capture_output=True, text=True, timeout=60)
	assert result.returncode == 0 and not save.exists(), result
	source = int(re.search(r'League Score league: race \d+/\d+, track (\d+)', result.stdout)[1])
	track = fields(args.gamedir / f'Game/Track{source}.trk')['trackname']
	directory = root / 'league-cli'
	seed(directory, track)

	with driving(command + ['--debug-records', str(directory)], log, env, r'\| [28] cars \|'):
		pass  # Window close saves records without awarding League points.

	rows = read(args, directory, 0, track)
	assert count(rows, 'League score') == 1 and rows[0][4:] == (1000000, 0), rows[:2]
	assert len(list(directory.iterdir())) == 1
	print('Standalone League: window exit saves the actual human Best Lap identity without a Single Race table', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path)
	parser.add_argument('--launchers-only', action='store_true')
	args = parser.parse_args()

	def check(root):
		if not args.launchers_only:
			run(args, root)

		launchers(args, root)

	if args.output is None:
		with tempfile.TemporaryDirectory(prefix='mh-highscore-races-') as directory:
			check(Path(directory))
	else:
		args.output.mkdir(parents=True, exist_ok=True)
		check(args.output)


if __name__ == '__main__':
	main()
