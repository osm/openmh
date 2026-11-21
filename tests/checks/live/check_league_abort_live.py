#!/usr/bin/env python3
"""Check aborted League results advance the saved season only on acceptance.

Run under xvfb-run. Uses the production launcher, temporary LGF storage,
real X11 input, and the native filename-keyed LGF decoder.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.menu import diagnostic, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def race_window(process, log, race):
	found = []

	def ready():
		if log.read_text().count('camera-frame columns:') < race + 1:
			return False

		result = subprocess.run(
			['xdotool', 'search', '--all', '--onlyvisible', '--pid', str(process.pid),
				'--name', r'\| 8 cars \|'],
			capture_output=True,
			text=True,
			timeout=COMMAND_TIMEOUT)

		# This standalone launcher closes its SDL video subsystem between
		# races; X11 can reuse the same numeric window ID after reconnection.
		found[:] = [int(value) for value in result.stdout.split()
			if 'Race results' not in xdo('getwindowname', value)]

		return bool(found)

	wait(process, log, ready)

	return found[-1]


def decode(build, path):
	text = subprocess.check_output([str(build_executable(build, 'mh-tbl-decode')), str(path), '--key', path.name], text=True, timeout=COMMAND_TIMEOUT)
	header = {}
	human = {}
	target = header

	for line in text.splitlines():
		if not line or line.startswith('//'):
			continue

		name, value = line.split('\t', 1)

		if name == 'HumanPlayer':
			assert not human, 'multiple humans in fixture'
			target = human
		elif name == 'AIPlayer':
			target = {}

		target[name] = value

	return header, human


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-league-abort-') as temporary:
		root = Path(temporary)
		path = root / 'Abort.lgf'
		log = root / 'league.log'
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		resume = [str(build_executable(args.build, 'openmotor')), '--debug-league', '--assets', str(args.gamedir), '--debug-save', str(path),
			'--debug-no-settings', '--debug-no-records', '--debug-mute', '--debug-camera-trace', '1', '--debug-window-titles', '1']
		command = resume + ['--debug-new', 'Abort league', '--debug-player', 'Abort driver']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				# Division three has two scheduled races. Accept the first,
				# close the second unaccepted, resume, then accept with Esc.
				for index, (race, name) in enumerate(((0, 'Return'), (1, None), (1, 'Escape'))):
					window = race_window(process, log, index)
					xdo('windowfocus', '--sync', window)
					wait_frames(process, log, frames(log), 6)
					saved = path.read_bytes()
					header, human = decode(args.build, path)
					assert int(header['RaceNumber']) == race and int(human['Score']) == race, (
						header,
						human,
					)

					for action in ('Escape', 'Up', 'Return'):
						key(window, action, process, log)

					wait_frames(process, log, frames(log), 3)
					assert 'Race results' in xdo('getwindowname', window)
					assert 'Pos 8/8' in xdo('getwindowname', window)
					assert path.read_bytes() == saved, 'unaccepted abort changed LGF'

					if name is not None:
						xdo('keydown', name)

						try:
							wait(process, log, lambda: path.read_bytes() != saved)
						finally:
							xdo('keyup', name)

						header, human = decode(args.build, path)
						assert int(header['RaceNumber']) == race + 1 and int(human['Score']) == race + 1, (
							header,
							human,
						)
						assert all(human[field] == '0' for field in ('Wins', 'Seconds', 'Thirds')), human
					else:
						close_window(window)
						assert process.wait(timeout=30) == 0, diagnostic(log)
						assert path.read_bytes() == saved, 'window close awarded an unaccepted abort'
						process = subprocess.Popen(resume, stdout=stream, stderr=stream, env=env)

				season = window_for(process, log, 'Motorhead season standings')
				saved = path.read_bytes()
				close_window(season)
				assert process.wait(timeout=30) == 0, diagnostic(log)
				assert path.read_bytes() == saved, 'season close changed the confirmed result'
				assert not list(root.glob('*.tmp.*'))
				print('League abort: human last, Enter/Escape each award once, unaccepted close preserves LGF, resumption reaches season standings')
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


if __name__ == '__main__':
	main()
