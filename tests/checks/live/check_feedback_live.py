#!/usr/bin/env python3
"""Check saved feedback preference and device reporting in real X11 races.

Run without a physical feedback device. Mock-device tests cover effect output;
this check covers the normal playable path when that hardware is absent.
"""

from tests.checks.support.build import build_executable
import argparse
import os
import subprocess
import time
from pathlib import Path
import tempfile

from tests.checks.support.x11 import close_window
from tests.checks.support.x11 import xdo


def run_live(command, environment, log):
	with log.open('wb') as output:
		process = subprocess.Popen(command, env=environment, stdout=output,
			stderr=subprocess.STDOUT)

		try:
			window = subprocess.check_output(
				['xdotool', 'search', '--sync', '--pid', str(process.pid),
					'--name', 'Motorhead'], timeout=30).splitlines()[-1].decode()
			xdo('windowfocus', '--sync', window)
			deadline = time.monotonic() + 30

			while ' cars |' not in xdo('getwindowname', window):
				assert process.poll() is None and time.monotonic() < deadline, log.read_text()
				time.sleep(.05)

			# Software presentation can publish its first title before focus
			# arrives. Deliver focus and let a normal input/race frame run.
			xdo('windowfocus', '--sync', window)
			time.sleep(.5)

			if ' | PAUSED' in xdo('getwindowname', window):
				for _ in range(2):
					xdo('keydown', 'Escape')
					time.sleep(.2)
					xdo('keyup', 'Escape')
					time.sleep(.2)

			deadline = time.monotonic() + 10

			while ' | PAUSED' in xdo('getwindowname', window):
				assert process.poll() is None and time.monotonic() < deadline, log.read_text()
				time.sleep(.05)

			time.sleep(.5)
			close_window(window)
			assert process.wait(timeout=30) == 0, log.read_text()
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-feedback-live-') as temporary:
		root = Path(temporary)
		controls = root/'Controls'
		controls.mkdir()
		profile = controls/'Keyboard.clo'
		environment = dict(os.environ, SDL_VIDEO_DRIVER='x11',
			SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
			'--debug-car', '1', '--debug-track', '1', '--debug-opponents', '7',
			'--debug-no-settings', '--debug-no-records', '--debug-controls', str(controls), '--debug-mute', '--debug-window-titles', '1']

		for renderer in ('glide', 'software'):
			for enabled in ('Off', 'On'):
				original = f'LayoutName Keyboard\nForceFeedback {enabled}\n'.encode()
				profile.write_bytes(original)
				log = root/f'{renderer}-{enabled}.log'
				run_live(command+['--renderer', renderer], environment, log)
				text = log.read_text()
				expected = 'Force feedback: impact=unavailable road=unavailable spring=unavailable'

				if enabled == 'On':
					assert expected in text, text
				else:
					assert 'Force feedback:' not in text, text

				assert profile.read_bytes() == original

	print('X11 feedback On/Off, absent-device reporting, eight-car Glide/software races and clean exits passed')


if __name__ == '__main__':
	main()
