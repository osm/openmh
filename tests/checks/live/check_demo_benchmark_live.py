#!/usr/bin/env python3
"""Run rendered benchmarks, replace reports and check cancellation/write failure.

Run under xvfb-run. All settings, reports and logs stay in a temporary root.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from tests.checks.live.check_legacy_names_live import write_cfg
from tests.checks.support.settings import read_cfg
from tests.checks.support.menu import window_for, diagnostic
from tests.checks.support.x11 import close_window, xdo, key, frames, wait_frames


def run(args, root, demo, cancel=False):
	cfg = root/'Settings.CFG'
	write_cfg(cfg, dict(ScreenDimension=b'320 200', DetailMode=b'Low',
		Renderer=b'Software' if args.renderer == 'software' else b'Glide',
		SFXVolume=b'0', MusicVolume=b'0', MenuConfirmation=b'Off', CarView=b'Outside'))
	log = root/(demo+'.log')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--debug-demo', str(args.gamedir/'Demos'/demo),
		'--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-benchmark', '--debug-mute', '--debug-no-records', '--debug-camera-trace', '1', '--debug-window-titles', '1']

	with log.open('w') as stream:
		process = subprocess.Popen(command, cwd=root, env=env, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, r' \| ')
			xdo('windowfocus', '--sync', window)

			if cancel:
				close_window(window)
			else:
				wait_frames(process, log, frames(log), 4)
				trace = [line.split() for line in log.read_text().splitlines()
					if line.startswith('camera-frame ') and
					not line.startswith('camera-frame columns:')]

				# Xvfb's startup focus loss latches the local pause state.
				if trace[-1][6] == '1':
					key(window, 'Escape', process, log)
					key(window, 'Escape', process, log)

				key(window, 'F1', process, log)
				key(window, 'F4', process, log)

			result = process.wait(timeout=180)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	assert read_cfg(cfg)['CarView'] == 'Outside', read_cfg(cfg)

	if not cancel:
		trace = [line.split() for line in log.read_text().splitlines()
			if line.startswith('camera-frame ') and
			not line.startswith('camera-frame columns:')]
		assert trace and all(row[5] == '2' for row in trace), 'benchmark lost track camera'

	return result, log


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('glide', 'software'), default='software')
	args = parser.parse_args()
	args.build, args.gamedir = args.build.resolve(), args.gamedir.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-benchmark-live-') as directory:
		root = Path(directory)
		report = root/'Fps.txt'

		for name in ('Bench2.MDE', 'Bench1.MDE'):
			code, log = run(args, root, name)
			assert code == 0, diagnostic(log)
			assert 'saved Fps.txt' in log.read_text(), diagnostic(log)
			data = (args.gamedir/'Demos'/name).read_bytes()
			count = struct.unpack_from('<I', data, 28)[0]
			last = struct.unpack_from('<I', data, len(data)-(8+30*count)+4)[0]
			expected_frames = [(last+29)//30-1]
			text = report.read_text()
			frames = list(map(int, re.findall(r'^Frames:\s+(\d+)$', text, re.M)))
			assert frames == expected_frames, (frames, expected_frames)
			fps = re.findall(r'^Frames/Sec:\s+([\d.]+)$', text, re.M)
			assert len(fps) == 1 and float(fps[0]) > 0, text
			assert text.count('*** Motorhead Benchmark Results') == len(expected_frames)
			assert ('Native software' if args.renderer == 'software' else 'Native Glide-style') in text
			print(f'{args.renderer} {name}: {frames[-1]} measured frames; report replaced', flush=True)

		saved = report.read_bytes()
		code, log = run(args, root, 'Bench2.MDE', cancel=True)
		assert code == 0 and report.read_bytes() == saved, diagnostic(log)
		report.rename(root/'saved-report.txt')
		report.mkdir()
		code, log = run(args, root, 'Bench2.MDE')
		assert code != 0 and 'cannot write benchmark report' in log.read_text(), diagnostic(log)
		assert (root/'saved-report.txt').read_bytes() == saved
		print('cancellation preserves report; failed report publication is reported', flush=True)


if __name__ == '__main__':
	main()
