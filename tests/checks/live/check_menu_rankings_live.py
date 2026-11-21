#!/usr/bin/env python3
"""Exercise Main/Rankings navigation, disk scores and malformed-file recovery.

Run under xvfb-run. Uses isolated storage, real SDL key input and captures;
score files are never changed by the frontend.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_credits_live import capture
from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, wait


def payload(name):
	return b''.join(struct.pack('<I24s16s3I', i % 2, name if i == 0 else f'Driver {i+1}'.encode(),
		b'Serpent', 0, 61345 + i * 1000, 310999 + i * 5000) for i in range(10))


def name_bounds(frame, layout):
	width, height, rgb = frame
	x, _, y, _ = struct.unpack_from('<4i', layout, 11 * 1024 + 16 * 32)
	right = struct.unpack_from('<i', layout, 11 * 1024 + 17 * 32)[0]

	# Palette index1 has fixed red255/green100. Name row sits below artwork,
	# and the next column bounds the scan independently of expected text.
	xs = [col for row in range(y * height // 400, (y + 12) * height // 400)
		for col in range(x * width // 640, right * width // 640)
		if rgb[(row * width + col) * 3:(row * width + col) * 3 + 2] == bytes((255, 100))]
	assert xs, 'missing high-score name pixels'

	return min(xs), max(xs)


def run(args, root):
	for name in ('controls', 'addresses', 'scores'):
		(root / name).mkdir(exist_ok=True)

	log = root / 'live.log'
	score = root / 'scores/srmm05redrock.mhi'
	score.write_bytes(payload(b'Native Score'))
	original = score.read_bytes()
	layout = (args.gamedir / 'Data/sprpos.dta').read_bytes()
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-no-settings', '--audio', '0',
		'--debug-recordings', str(root / 'recordings'), '--debug-controls', str(root / 'controls'), '--debug-addresses', str(root / 'addresses'),
		'--debug-window-titles', '1']

	with frontend(command, ['--debug-selection', '2', '--debug-records', str(root / 'scores')], log, env) as (process, window):
		menu_key(window, 'Return')
		wait(process, log, lambda: 'entry 1, load 1, file SRMM05RedRock.MHI, present 1' in log.read_text())
		time.sleep(.5)
		loaded = capture(window, root / 'loaded.png')
		wide = name_bounds(loaded, layout)
		menu_key(window, 'Return')  # No action on this page.
		assert log.read_text().count('Rankings: entry ') == 1, diagnostic(log)

		for key in ('Up', 'Up'):
			menu_key(window, key)

		assert 'file BRMM05RedRock.MHI, present 0' in log.read_text(), diagnostic(log)
		assert 'file BLMM00RedRock.MHI, present 0' in log.read_text(), diagnostic(log)
		capture(window, root / 'best-lap.png')

		for key in ('Right', 'Right', 'Down'):
			menu_key(window, key)

		# Best Lap cannot advance past track, so Down changes that track.
		assert re.search(r'load 4, file BLMM00(?!RedRock)[^.]+\.MHI', log.read_text()), diagnostic(log)

		for key in ('Left', 'Down', 'Down', 'Right', 'Right', 'Down', 'Right', 'Down'):
			menu_key(window, key)

		assert re.search(r'file SRHM10[^.]+\.MHI, present 0', log.read_text()), diagnostic(log)
		capture(window, root / 'single-hard.png')
		assert score.read_bytes() == original
		menu_key(window, 'Escape')
		score.write_bytes(payload(b'W'))
		modified = score.read_bytes()
		menu_key(window, 'Return')
		wait(process, log, lambda: re.search(r'entry 2, load \d+, file SRMM05RedRock.MHI, present 1', log.read_text()))
		time.sleep(.5)
		narrow = name_bounds(capture(window, root / 'reloaded.png'), layout)
		assert narrow[1] - narrow[0] < wide[1] - wide[0], (wide, narrow)
		assert score.read_bytes() == modified

		# A damaged wrapper is reported in a dismissible requester. No rows
		# from the previous table survive, and the damaged file is retained.
		menu_key(window, 'Escape')
		score.write_bytes(b'TBL\x09broken')
		menu_key(window, 'Return')
		wait(process, log, lambda: 'Rankings: unsupported TBL resource header' in log.read_text())
		time.sleep(.5)
		capture(window, root / 'load-error.png')
		menu_key(window, 'Return')
		menu_key(window, 'Escape')

	assert 'Rankings: entries 3,' in log.read_text() and 'page main' in log.read_text(), diagnostic(log)
	assert score.read_bytes() == b'TBL\x09broken' and list((root / 'scores').iterdir()) == [score]

	# The ordinary default directory is shared with existing CL/CR records.
	preferred = root / 'userdata'
	preferred.mkdir(parents=True, exist_ok=True)
	(preferred / score.name).write_bytes(original)

	with frontend(command, ['--debug-page', 'rankings'], root / 'default.log', env) as (process, window):
		wait(process, root / 'default.log', lambda: 'file SRMM05RedRock.MHI, present 1' in (root / 'default.log').read_text())
		menu_key(window, 'Escape')

	assert (preferred / score.name).read_bytes() == original

	# Diagnostic capture defaults to embedded tables, independent of user data.
	result = subprocess.run(
		command + ['--debug-page', 'rankings', '--debug-frames', '60', '--debug-capture', str(root / 'direct.bmp')],
		env=env,
		capture_output=True,
		text=True,
		timeout=60,
	)
	assert result.returncode == 0 and 'file SRMM05RedRock.MHI, present 0' in result.stdout, result
	(root / 'direct.log').write_text(result.stdout + result.stderr)
	print('Rankings X11: Main entry, all reachable selectors, ignored Enter, retained focus/re-entry, '
		'visible disk-name reload, malformed-file recovery, per-user reads and capture isolation passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path)
	args = parser.parse_args()

	if args.output is None:
		with tempfile.TemporaryDirectory(prefix='mh-rankings-live-') as directory:
			run(args, Path(directory))
	else:
		args.output.mkdir(parents=True, exist_ok=True)
		run(args, args.output)


if __name__ == '__main__':
	main()
