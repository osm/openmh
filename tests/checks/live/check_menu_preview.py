#!/usr/bin/env python3
"""Check SDL main-menu captures against indexed output and optional X11 input.

Capture comparison checks the presentation adapter. Unit tests cover menu
drawing and state. Use --live under
xvfb-run for real X11 keyboard/close events. Generated reports stay outside Git.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from tests.checks.support.x11 import close_window


from tests.checks.support.images import bmp_rgb


def live(command, directory):
	environment = dict(os.environ, SDL_VIDEO_DRIVER='x11')
	log = directory / 'live.log'

	with log.open('wb') as output:
		process = subprocess.Popen(command, env=environment, stdout=output, stderr=subprocess.STDOUT)

		try:
			window = subprocess.check_output(
				['xdotool', 'search', '--sync', '--pid', str(process.pid),
					'--name', 'Motorhead main-menu preview'],
				timeout=30,
			).splitlines()[-1].decode()
			subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True, timeout=10)
			time.sleep(.6)  # Original reset intentionally discards four polls.

			for key, delay in [('Right', .8), ('Left', .8), ('Right', .08)]:
				assert process.poll() is None, log.read_text()
				subprocess.run(['xdotool', 'keydown', key], check=True, timeout=10)
				time.sleep(delay)
				subprocess.run(['xdotool', 'keyup', key], check=True, timeout=10)
				time.sleep(.2)

			close_window(window)
			assert process.wait(timeout=30) == 0, log.read_text()
			assert re.search(r'Menu preview: selection 1,', log.read_text()), log.read_text()
		finally:
			if process.poll() is None:
				process.terminate()
				process.wait(timeout=10)

	print('X11 arrows, repeated-key boundary clamping and window close passed')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--live', action='store_true')
	parser.add_argument('--audio', action='store_true', help='require audio; use SDL_AUDIO_DRIVER=dummy for headless checks')
	parser.add_argument('--report', type=Path)
	parser.add_argument('--compare', type=Path)
	args = parser.parse_args()
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-no-settings',
		'--debug-window-titles', '1']

	if args.audio:
		command.append('--debug-require-audio')

	results = []

	with tempfile.TemporaryDirectory(prefix='mh-menu-preview-') as temporary:
		root = Path(temporary)
		command += ['--user-data', str(root / 'userdata')]

		for selected in range(5):
			for frames in (1, 2, 10, 120):
				capture, raw = root / 'screen.bmp', root / 'screen.idx'
				output = subprocess.run(
					command + ['--debug-selection', str(selected), '--debug-capture', str(capture),
						'--debug-indexed', str(raw), '--debug-frames', str(frames)],
					capture_output=True,
					text=True,
					timeout=60,
				)
				assert output.returncode == 0, (selected, frames, output.stdout, output.stderr)
				indices = raw.read_bytes()
				assert len(indices) == 256768
				expected = b''.join(indices[i * 3:i * 3 + 3] for i in indices[768:])
				actual = bmp_rgb(capture)
				assert actual == expected, (
					selected,
					frames,
					'SDL pixels differ from exported palette indices',
				)
				results.append({'selection': selected, 'frames': frames, 'rgb_sha256': hashlib.sha256(actual).hexdigest(),
					'state': output.stdout.strip()})

		# Early background-only frames match across selections; settled labels
		# and needle direction must differ for every original main choice.
		assert len({r['rgb_sha256'] for r in results if r['frames'] == 1}) == 1
		assert len({r['rgb_sha256'] for r in results if r['frames'] == 120}) == 5

		if args.live:
			live(command, root)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build capture/state mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} SDL main-menu captures matched all 256,000 indexed/palette pixels')


if __name__ == '__main__':
	main()
