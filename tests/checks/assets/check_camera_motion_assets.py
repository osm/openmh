#!/usr/bin/env python3
"""Check native camera LWMO parsing against every original TRK selection."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import struct
import subprocess


from tests.checks.support.process import COMMAND_TIMEOUT


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--game', type=Path, required=True)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()
	files = {str(p.relative_to(args.game)).lower(): p
		for p in args.game.rglob('*') if p.is_file()}
	total = 0

	for track in range(1, 9):
		text = subprocess.check_output(
			[str(build_executable(args.build, 'mh-tbl-decode')), str(files[f'game/track{track}.trk'])], text=True, timeout=COMMAND_TIMEOUT)
		selections = [line.split()[1] for line in text.splitlines()
			if line.split() and line.split()[0].lower() == 'camerasplinename']
		assert len(selections) == 1, (track, selections)
		path = files[selections[0].replace('\\', '/').lower()]
		lines = [line.strip() for line in path.read_text().splitlines() if line.strip()]
		assert lines[:3] == ['LWMO', '1', '9'], path
		count = int(lines[3])
		assert len(lines) == 4 + 2*count, path
		expected = []

		for i in range(count):
			channels = list(map(float, lines[4+2*i].split()))
			metadata = list(map(float, lines[5+2*i].split()))
			assert len(channels) == 9 and len(metadata) == 5, (path, i)
			expected.append(struct.pack('<14f', *(metadata + channels)))

		result = subprocess.check_output(
			[str(build_executable(args.build, 'test-camera-motion')), str(path)], text=True, timeout=COMMAND_TIMEOUT)
		actual = [struct.pack('<14f', *map(float, line.split()))
			for line in result.splitlines()]
		assert actual == expected, path
		total += count
		print(f'Track {track}: {count} records, all 14 binary32 fields agree')

	print(f'{total} camera records across eight original tracks passed')


if __name__ == '__main__':
	main()
