#!/usr/bin/env python3
"""Capture original finish models through the real race drawing paths.

Run under xvfb-run when no X11 display is available. This exercises rendering
and presentation gates, not physical race completion or a live network peer.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


from tests.checks.support.process import COMMAND_TIMEOUT


def pixels(path):
	data = path.read_bytes()
	assert data[:3] == b'\0\0\2' and data[16] == 16, path
	width, height = struct.unpack_from('<HH', data, 12)
	assert (width, height) == (640, 400), (path, width, height)
	assert len(data) == 18 + width*height*2, path

	return tuple(v[0] for v in struct.iter_unpack('<H', data[18:]))


def check(args, output):
	for profile, name in enumerate(('glide', 'software', 'direct3d', 'powervr')):
		prefix = output/name
		subprocess.run([str(build_executable(args.build, 'mh-finish-render')), str(args.gamedir),
			str(profile), str(prefix)], check=True, timeout=COMMAND_TIMEOUT)
		images = [pixels(output/f'{name}-{i}.tga') for i in range(10)]
		counts = [sum(a != b for a, b in zip(images[0], images[i]))
			for i in (1, 2, 3)]
		assert all(10000 < count < 100000 for count in counts), (name, counts)
		assert len({images[i] for i in (1, 2, 3)}) == 3, name

		# Non-podium, unfinished, practice, and track-camera gates leave the
		# same underlying frame. The fixture checks callback suppression too.
		for i in (4, 5, 6, 7):
			assert images[i] == images[0], (name, 'hidden sign changed pixels', i)

		# Earlier motion must produce a distinct, visible arrival pose.
		assert images[9] != images[0] and images[9] != images[1], name
		print(f'{name}: three distinct podium signs ({counts} changed pixels), '
			'hidden gates and arrival pose passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path)
	args = parser.parse_args()

	if args.output:
		args.output.mkdir(parents=True, exist_ok=True)
		check(args, args.output)
	else:
		with tempfile.TemporaryDirectory(prefix='mh-finish-render-') as directory:
			check(args, Path(directory))


if __name__ == '__main__':
	main()
