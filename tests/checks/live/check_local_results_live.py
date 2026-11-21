#!/usr/bin/env python3
"""Check local results acceptance/close and retained SDL parent objects.

Run under xvfb-run. The native fixture finishes a real eight-car race before
opening the renderer; no production control or simulation hooks are added.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.menu import diagnostic, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--renderer', choices=('glide', 'software', 'direct3d', 'powervr'), default='glide')
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--prepare-timeout', type=float, default=60,
		help='seconds to finish each offline race before opening its window')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-local-results-') as temporary:
		root = Path(temporary)
		log = root/'results.log'
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		with log.open('w') as stream:
			process = subprocess.Popen([str(build_executable(args.build, 'mh-local-results-live')),
				'--debug-window-titles', '1',
				'--debug-camera-trace', '1',
				'--debug-music-trace', '1', str(args.gamedir), args.renderer],
				stdout=stream, stderr=stream, env=env)

			try:
				for run in range(8):
					timeout = args.prepare_timeout if run in (0, 5, 7) else 60
					wait(process, log, lambda: f'Results fixture: run {run}' in log.read_text(),
						seconds=timeout)

					if run == 5:
						wait(process, log, lambda: 'Results fixture: returned 5' in log.read_text())
						continue

					race = window_for(process, log, r'\| 1 cars \|' if run == 6 else r'\| 8 cars \|')
					xdo('windowfocus', '--sync', race)

					if run < 2:
						# Original local finish waits ten simulated seconds.
						# Use game-clock milliseconds, not physics slice
						# counts: original sampling changes slices with FPS.
						section = log.read_text().split(f'Results fixture: run {run}\n')[1]
						initial = float(next(line for line in section.splitlines()
							if line.startswith('Results fixture: intro clock ')).split()[-1])
						wait(process, log, lambda: 'Race results' in xdo('getwindowname', race))
						section = log.read_text().split(f'Results fixture: run {run}\n')[1]

						# The writer can be halfway through a buffered trace row.
						complete = section.rsplit('\n', 1)[0]
						last = next(line for line in reversed(complete.splitlines())
							if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:'))
						assert float(last.split()[-1])-initial >= 9999, (initial, last)
						wait_frames(process, log, frames(log), 3)
						name = 'Return' if run == 0 else 'Escape'
						xdo('keydown', name)

						try:
							wait(process, log, lambda: f'Results fixture: returned {run}' in log.read_text())
						finally:
							xdo('keyup', name)
					elif run == 2:
						close_window(race)
					else:
						wait_frames(process, log, frames(log), 6)

						for name in ('Escape', 'Up'):
							key(race, name, process, log)

						xdo('keydown', 'Return')

						try:
							if run == 6:
								wait(process, log, lambda: 'Results fixture: returned 6' in log.read_text())
							else:
								if run == 3:
									wait(process, log, lambda: 'intentional fixture save failure' in log.read_text())

									# Keep Quit held past the keyboard repeat delay.
									time.sleep(.8)

								wait_frames(process, log, frames(log), 8)
								assert 'Race results' in xdo('getwindowname', race)
								assert f'Results fixture: returned {run}' not in log.read_text()
								assert run != 3 or log.read_text().count('intentional fixture save failure') == 1
						finally:
							xdo('keyup', 'Return')

						if run == 4:
							close_window(race)
						elif run in (3, 7):
							wait_frames(process, log, frames(log), 3)
							xdo('keydown', 'Escape')

							try:
								wait(process, log, lambda: f'Results fixture: returned {run}' in log.read_text())
							finally:
								xdo('keyup', 'Escape')

				assert process.wait(timeout=30) == 0, log.read_text()[-12000:]
				profile = {'glide': 'Glide', 'software': 'Software', 'direct3d': 'Direct3D', 'powervr': 'PowerVR'}[args.renderer]
				renderers = [line for line in log.read_text().splitlines()
					if line.startswith('Race renderer:')]
				assert len(renderers) >= 7 and set(renderers) == {f'Race renderer: {profile}'}, renderers
				assert 'returned 0, completed 1, quit 0, commits 1' in log.read_text()
				assert 'returned 1, completed 1, quit 0, commits 2' in log.read_text()
				assert 'returned 2, completed 0, quit 1, commits 2' in log.read_text()
				assert 'returned 3, completed 1, quit 0, commits 3' in log.read_text()
				assert 'returned 4, completed 0, quit 1, commits 3' in log.read_text()
				assert 'returned 5, completed 1, quit 0, commits 4' in log.read_text()
				assert 'returned 6, completed 1, quit 0, commits 5' in log.read_text()
				assert 'returned 7, completed 1, quit 0, commits 6' in log.read_text()
				finished_quit = log.read_text().split('Results fixture: run 7\n')[1]
				assert finished_quit.count('Results music: CD track 2') == 1
				finished = log.read_text().split('Results fixture: run 0\n')[1].split('Results fixture: run 1\n')[0]
				assert finished.count('Results music: CD track 2') == 1
				retry = log.read_text().split('Results fixture: run 3\n')[1].split('Results fixture: run 4\n')[0]
				assert 'Results music: CD track 2' not in retry
				assert retry.count('Music continuation begin:') == 1
				assert retry.index('Music continuation begin:') < retry.index('intentional fixture save failure')
				assert 'Music continuation close: track ' in retry and ', failed 0' in retry
				print('Local results: Enter/Escape, patched abort ranks, direct abort save, held keys, immutable save retry, window close and both Time Attack exits and Quit after finishing pass; parent SDL objects survive all eight')
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
