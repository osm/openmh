#!/usr/bin/env python3
"""Exercise prepared demo selection, race/abort/return and saved settings.

Run under Xvfb with xdotool. This tests real menu input and rendered sessions;
aborted quick races return directly, as in the patched game. Completed races
and their result boards have a separate qualification.
The input installers are never needed by this check.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.installation import prepare_view
from tests.checks.live.check_window_focus_live import expect_focus
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo

RENDERERS = ('software', 'glide', 'direct3d', 'powervr', 'openmh')
SAVED_RENDERERS = ('Software', 'Glide', 'Direct3D', 'PowerVR', 'OpenMH')
RACE_RENDERERS = ('Software', 'Glide', 'Direct3D', 'PowerVR', 'OpenMH')


def stop(process):
	if process.poll() is None:
		process.terminate()

		try:
			process.wait(timeout=5)
		except subprocess.TimeoutExpired:
			process.kill()
			process.wait(timeout=5)


def run_session(binary, assets, root, replay, renderer=None):
	log = root / ('reload.log' if replay else 'race.log')
	cfg = root / 'MotorHead.CFG'
	game = root / 'installation'

	if not replay:
		prepare_view(game, assets)
		cfg.write_text('ScreenDimension 640 400\n')

	if renderer == 'software' and not replay:
		# Switching from a saved GPU size must choose a valid software mode.
		cfg.parent.mkdir(parents=True, exist_ok=True)
		cfg.write_text('Renderer Glide\nScreenDimension 1600 1200\n')

	environment = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(binary), '--assets', str(game), '--debug-settings', str(cfg),
		'--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0', '--debug-no-records', '--debug-camera-trace', '1', '--debug-window-titles', '1']

	with log.open('w') as stream:
		process = subprocess.Popen(command, stdout=stream, stderr=stream,
			cwd=root, env=environment)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)

			if replay:
				before = read_cfg(cfg)
				menu_key(menu, 'Return')
			else:
				# Second car, next course, one lap, Hard, Manual, lower volume.
				for name in ('Down', 'Right', 'Down', 'Right', 'Down', 'Left',
					'Left', 'Down', 'Right', 'Down', 'Right', 'Down',
					'Down', 'Left'):
					menu_key(menu, name)

				if renderer is not None:
					# Row 8 selects a profile; the software case saves Glide.
					menu_key(menu, 'Down')
					initial = 'glide' if renderer == 'software' else 'openmh'
					steps = (RENDERERS.index(renderer) - RENDERERS.index(initial)) % len(RENDERERS)

					for _ in range(steps):
						menu_key(menu, 'Right')

					menu_key(menu, 'Up')

				# Resolution follows Renderer. Exercise both directions.
				for name in ('Down', 'Down', 'Left', 'Right', 'Right'):
					menu_key(menu, name)

				# Select Start without activating Quit.
				for _ in range(9):
					menu_key(menu, 'Up')

				menu_key(menu, 'Return')

			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)
			saved = read_cfg(cfg)
			assert saved['TrackLaps'] == '1', saved
			assert saved['Difficulty'] == 'Hard', saved
			assert saved['CarTransmission'] == 'Manual', saved
			assert saved['RecordRace'] == 'Off', saved
			choice = RENDERERS.index(renderer or 'openmh')
			assert saved['Renderer'] == SAVED_RENDERERS[choice], saved
			expected_size = '320 200' if renderer == 'software' else '640 480'
			assert saved['ScreenDimension'] == expected_size, saved
			assert 'Race viewport: ' + expected_size.replace(' ', 'x') in log.read_text(), diagnostic(log)

			if choice == RENDERERS.index('software'):
				assert int(saved['ScreenDimension'].split()[0]) <= 1023, saved

			assert 'Race renderer: ' + RACE_RENDERERS[choice] in log.read_text(), diagnostic(log)
			assert 'entrants 8' in log.read_text(), diagnostic(log)

			if replay:
				assert saved == before, (saved, before)
			else:
				# Pause/resume, resize, then use the existing restart action.
				for name in ('Escape', 'Escape'):
					key(race, name, process, log)

				xdo('windowsize', race, 800, 600)
				wait_frames(process, log, frames(log), 4)

				for name in ('Escape', 'Down', 'Return'):
					key(race, name, process, log)

				wait_frames(process, log, frames(log), 6)

			# Restart retains the selected row; close and reopen resets it.
			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			assert window_for(process, log, 'main-menu preview') == menu
			expect_focus(process, log, menu)
			assert 'Race return: completed 1, quit 0' in log.read_text(), diagnostic(log)
			time.sleep(.4)
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)

			for failure in ('AddressSanitizer', 'runtime error:'):
				assert failure not in log.read_text(), diagnostic(log)

			return read_cfg(cfg)
		except Exception:
			print(diagnostic(log), flush=True)

			raise
		finally:
			stop(process)


def capture_profile(build, assets, root, renderer):
	"""Check the eight-car scene and board art, not a physical race finish."""

	for board in (False, True):
		name = 'results' if board else 'race'
		capture = root / (name + '.bmp')
		command = [str(build_executable(build, 'openmotor')), '--debug-race', '--assets', str(assets),
			'--renderer', renderer, '--debug-car', '1', '--debug-track', '1',
			'--debug-opponents', '7', '--debug-ai', '2', '--debug-ticks', '300',
			'--debug-no-settings', '--debug-no-records', '--debug-mute', '--debug-capture', str(capture), '--debug-window-titles', '1']

		if board:
			command.append('--debug-results')

		# A Windows GUI launch can have no writable console for standings.
		with open(os.devnull, 'rb') as readonly:
			result = subprocess.run(command, stdout=readonly, stderr=subprocess.PIPE,
				text=True, timeout=150)

		assert result.returncode == 0, (renderer, name, result.stderr)
		assert capture.read_bytes()[:2] == b'BM' and capture.stat().st_size > 100000

		for failure in ('AddressSanitizer', 'runtime error:'):
			assert failure not in result.stderr, result.stderr


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--prepared', type=Path, required=True)
	parser.add_argument('--edition', action='append')
	parser.add_argument('--renderer', choices=RENDERERS, action='append',
		help='Select and persist a menu profile; also capture scene/board art')
	args = parser.parse_args()
	binary = (build_executable(args.build, 'openmotor')).resolve()
	prepared = args.prepared.resolve()
	editions = args.edition or sorted(p.name for p in prepared.iterdir()
		if (p / 'motor.exe').is_file())
	assert editions

	with tempfile.TemporaryDirectory(prefix='mh-demo-menus-') as temporary:
		root = Path(temporary)

		for edition in editions:
			for renderer in args.renderer or [None]:
				session = root / edition / (renderer or 'default')
				session.mkdir(parents=True)

				if renderer:
					capture_profile(args.build.resolve(), prepared / edition, session, renderer)

				first = run_session(binary, prepared / edition, session, False, renderer)
				second = run_session(binary, prepared / edition, session, True, renderer)
				assert second == first, (edition, first, second)
				assert len([p for p in session.rglob('*') if p.suffix.lower() == '.cfg']) == 1
				print(f'{edition}/{renderer or "default"}: selection/resolution, 8-car race, pause, resize, '
					'restart, abort/menu return and settings relaunch passed', flush=True)


if __name__ == '__main__':
	main()
