#!/usr/bin/env python3
"""Exercise Main/Credits navigation and the temporary hidden text under X11.

Run under xvfb-run. Uses installed assets read-only, isolated user storage,
real SDL key input and X11 window captures. The retained Club TBL trigger is
covered by test-menu-credits; this checks the independent held-key shortcut.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib

from tests.checks.support.application import frontend
from tests.checks.support.menu import diagnostic, menu_key, wait
from tests.checks.support.x11 import xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def capture(window, path):
	"""Save the Xvfb TrueColor window as a portable PNG without extra packages."""

	dump = path.with_suffix('.xwd')
	subprocess.run(['xwd', '-id', str(window), '-silent', '-out', str(dump)], check=True, timeout=COMMAND_TIMEOUT)
	data = dump.read_bytes()
	header = struct.unpack_from('>25I', data)
	size, version, kind, depth, width, height = header[:6]
	assert version == 7 and kind == 2 and depth == 24 and header[11] == 32, header
	assert header[14:17] == (0xff0000, 0xff00, 0xff), header
	offset = size + header[19] * 12
	pitch = header[12]
	assert len(data) >= offset + height * pitch
	rgb = bytearray()

	for y in range(height):
		for x in range(width):
			index = offset + y * pitch + x * 4
			pixel = int.from_bytes(data[index:index + 4], 'little' if header[7] == 0 else 'big')
			rgb.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))

	def chunk(kind, value):
		return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value) & 0xffffffff)

	rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
	path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0))
		+ chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

	return width, height, bytes(rgb)


def heading_bounds(frame, y, spacing):
	width, height, rgb = frame

	# Font palette index 3 is fixed RGB 96/96/192. The first heading lies
	# below the artwork and above the first body line; background uses
	# indices 192..255. This checks visible text, not only the adapter log.
	xs = [x for row in range(y * height // 400, (y + spacing) * height // 400) for x in range(width)
		if rgb[(row * width + x) * 3:(row * width + x) * 3 + 3] == bytes((96, 96, 192))]
	assert xs, 'missing Credits heading pixels'

	return min(xs), max(xs)


def run(args, root):
	log = root / 'live.log'

	# xdotool's named Control_R also synthesizes Control_L. Use physical
	# keycodes so that the negative left/right-modifier case is meaningful.
	mapping = subprocess.check_output(['xmodmap', '-pke'], text=True, timeout=COMMAND_TIMEOUT)
	codes = {}

	for line in mapping.splitlines():
		for name in ('Control_L', 'Control_R', 'Alt_L', 'Alt_R'):
			if name in line.split()[3:]:
				codes.setdefault(name, int(line.split()[1]))

	assert len(codes) == 4, codes

	for name in ('controls', 'addresses'):
		(root / name).mkdir(exist_ok=True)

	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-no-settings',
		'--debug-no-records', '--audio', '0', '--debug-recordings', str(root / 'recordings'),
		'--debug-controls', str(root / 'controls'), '--debug-addresses', str(root / 'addresses'),
		'--debug-window-titles', '1']

	with frontend(command, ['--debug-selection', '4'], log, env) as (process, window):
		menu_key(window, 'Return')
		wait(process, log, lambda: 'Credits: entry 1, alternate 0, Club TBL 0' in log.read_text())
		time.sleep(.5)
		normal = capture(window, root / 'normal.png')
		xdo('windowfocus', '--sync', window)

		try:
			# Right Ctrl is not the original left-Ctrl binding.
			for key in ('Control_R', 'Alt_L', 'Alt_R'):
				xdo('keydown', codes[key])

			time.sleep(.3)
			assert 'alternate 1' not in log.read_text(), diagnostic(log)
			xdo('keyup', codes['Control_R'])
			xdo('keydown', codes['Control_L'])
			wait(process, log, lambda: 'Credits: entry 1, alternate 1, Club TBL 0' in log.read_text())
			time.sleep(.2)
			alternate = capture(window, root / 'alternate.png')
			layout = (args.gamedir / 'Data/sprpos.dta').read_bytes()
			x, _, y, _ = struct.unpack_from('<4i', layout, 18 * 1024 + 24 * 32)
			spacing = struct.unpack_from('<i', layout, 18 * 1024 + 25 * 32 + 8)[0] // 8
			normal_bounds = heading_bounds(normal, y, spacing)
			alternate_bounds = heading_bounds(alternate, y, spacing)
			assert normal_bounds[1] - normal_bounds[0] > alternate_bounds[1] - alternate_bounds[0]

			for bounds in (normal_bounds, alternate_bounds):
				assert abs(sum(bounds) / 2 - x * normal[0] / 640) <= 3, bounds

			# Releasing just right Alt restores normal text, without leaving.
			xdo('keyup', codes['Alt_R'])
			wait(process, log, lambda: log.read_text().count('Credits: entry 1, alternate 0, Club TBL 0') == 2)
		finally:
			for key in ('Control_L', 'Control_R', 'Alt_L', 'Alt_R'):
				xdo('keyup', codes[key])

		# Both return keys work and Main retains the Credits selection.
		menu_key(window, 'Escape')
		menu_key(window, 'Return')
		wait(process, log, lambda: 'Credits: entry 2, alternate 0, Club TBL 0' in log.read_text())
		menu_key(window, 'Return')
		menu_key(window, 'Return')
		wait(process, log, lambda: 'Credits: entry 3, alternate 0, Club TBL 0' in log.read_text())
		menu_key(window, 'Return')

	assert 'Credits: entries 3, page main' in log.read_text(), diagnostic(log)

	# The direct diagnostic entry uses the same owned artwork and callbacks.
	result = subprocess.run(
		command + ['--debug-page', 'credits', '--debug-frames', '60', '--debug-capture', str(root / 'direct.bmp')],
		env=env,
		capture_output=True,
		text=True,
		timeout=60,
	)
	assert result.returncode == 0 and 'Credits: entries 1, page credits' in result.stdout, result
	(root / 'direct.log').write_text(result.stdout + result.stderr)
	print('Credits X11: Main entry, both return keys, retained selection, exact held shortcut, '
		'release/re-entry without latching and direct capture passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path)
	args = parser.parse_args()

	if args.output is None:
		with tempfile.TemporaryDirectory(prefix='mh-credits-live-') as directory:
			run(args, Path(directory))
	else:
		args.output.mkdir(parents=True, exist_ok=True)
		run(args, args.output)


if __name__ == '__main__':
	main()
