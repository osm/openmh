#!/usr/bin/env python3
"""Check dialog presentation and real X11 confirmation/exit transitions.

This checks the SDL adapter and controller connection. Use --live under
xvfb-run. Reports contain deterministic captures only, not wall-clock-dependent
live animation state.
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


def live(command, root):
	cases = [
		('default-no', 1, ['Escape', 'Return', 'Right'], False, (1, 1, 2)),
		('escape-cancel', 1, ['Escape', 'Left', 'Escape', 'Right'], False, (1, 0, 2)),
		('accept', 1, ['Escape', 'Right', 'Left', 'Return'], True, (1, 0, 1)),
		('blocked-ok', 2, ['Escape', 'Left', 'Right', 'Return', 'Right'], False, (2, 0, 1)),
		('blocked-cancel', 2, ['Escape', 'Escape', 'Right'], False, (2, 0, 1)),
		('confirmation-off', 0, ['Escape'], True, None),
	]

	for name, confirmation, keys, exits, dialog in cases:
		log = root / (name + '.log')

		with log.open('wb') as output:
			process = subprocess.Popen(
				command + ['--debug-confirmation', str(confirmation)],
				env=dict(os.environ, SDL_VIDEO_DRIVER='x11'),
				stdout=output,
				stderr=subprocess.STDOUT,
			)

			try:
				window = subprocess.check_output(
					['xdotool', 'search', '--sync', '--pid', str(process.pid),
						'--name', 'Motorhead main-menu preview'],
					timeout=30,
				).splitlines()[-1].decode()
				subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True, timeout=10)
				time.sleep(.6)

				for key in keys:
					assert process.poll() is None, (name, log.read_text())
					subprocess.run(['xdotool', 'keydown', key], check=True, timeout=10)

					try:
						time.sleep(.04)  # A press, below the original repeat threshold.
					finally:
						subprocess.run(['xdotool', 'keyup', key], check=True, timeout=10)

					time.sleep(.3)

				if not exits:
					assert process.poll() is None, (name, 'unexpected exit', log.read_text())
					close_window(window)

				assert process.wait(timeout=10) == 0, (name, log.read_text())
				text = log.read_text()
				assert ('Exit page completed' in text) == exits, (name, text)
				selected = 0 if exits else 1
				assert re.search(rf'Menu preview: selection {selected},', text), (name, text)

				if dialog:
					kind, choice, answer = dialog
					assert f'Dialog: kind {kind}, visible 0, selected {choice}, answer {answer}' in text, (
						name,
						text,
					)
				else:
					assert 'Dialog:' not in text, (name, text)
			finally:
				if process.poll() is None:
					process.terminate()
					process.wait(timeout=10)

		print(f'X11 dialog {name} passed')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--live', action='store_true')
	parser.add_argument('--audio', action='store_true', help='require a device; SDL_AUDIO_DRIVER=dummy works headlessly')
	parser.add_argument('--report', type=Path)
	parser.add_argument('--compare', type=Path)
	args = parser.parse_args()
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
		*(['--debug-require-audio'] if args.audio else ['--audio', '0']), '--debug-window-titles', '1']
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-dialog-preview-') as temporary:
		root = Path(temporary)
		command += ['--user-data', str(root / 'userdata')]

		for dialog in ('exit', 'blocked'):
			for frames in (1, 2, 3, 6, 9, 120):
				capture, raw = root / 'screen.bmp', root / 'screen.idx'
				output = subprocess.run(
					command + ['--debug-dialog', dialog, '--debug-capture', str(capture),
						'--debug-indexed', str(raw), '--debug-frames', str(frames)],
					capture_output=True,
					text=True,
					timeout=60,
				)
				assert output.returncode == 0, (dialog, frames, output.stdout, output.stderr)
				indices = raw.read_bytes()
				assert len(indices) == 256768
				expected = b''.join(indices[i * 3:i * 3 + 3] for i in indices[768:])
				actual = bmp_rgb(capture)
				assert actual == expected, (dialog, frames, 'SDL pixels differ from indexed output')

				if frames > 1:
					kind, selected = (1, 1) if dialog == 'exit' else (2, 0)
					assert f'Dialog: kind {kind}, visible 1, selected {selected}, answer 0' in output.stdout

				results.append({'dialog': dialog, 'frames': frames, 'rgb_sha256': hashlib.sha256(actual).hexdigest(),
					'state': output.stdout.strip()})

		assert len({r['rgb_sha256'] for r in results if r['frames'] == 1}) == 1
		assert len({r['rgb_sha256'] for r in results if r['frames'] == 120}) == 2

		if args.live:
			live(command, root)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build capture/state mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2) + '\n')

	print(f'{len(results)} dialog captures matched all 256,000 indexed/palette pixels')


if __name__ == '__main__':
	main()
