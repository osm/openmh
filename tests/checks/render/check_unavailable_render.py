#!/usr/bin/env python3
"""Check real unavailable-car assets and offscreen native Vulkan composition."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import struct
import subprocess


def bmp(path):
	data = path.read_bytes()
	assert data[:2] == b'BM', path
	offset = struct.unpack_from('<I', data, 10)[0]
	width, height = struct.unpack_from('<ii', data, 18)
	bits = struct.unpack_from('<H', data, 28)[0]
	assert width == 960 and abs(height) == 640 and bits in (24, 32), path
	stride = ((width*bits+31)//32)*4
	center = offset+abs(height)//2*stride+width//2*(bits//8)

	return data[offset:], tuple(data[center:center+3])


def check_capture(prefix):
	frames = {name:bmp(Path(str(prefix)+'-'+name+'.bmp')) for name in (
		'normal', 'restored', 'unavailable', 'hidden', 'blend-0-visible',
		'blend-1-visible', 'blend-0-occluded', 'blend-1-occluded')}
	assert frames['normal'] == frames['restored'], (prefix, 'restoration')
	assert frames['unavailable'] != frames['normal'], (prefix, 'material transition')
	assert frames['unavailable'] != frames['hidden'], (prefix, 'unavailable is still visible')
	assert frames['blend-0-visible'] == frames['blend-1-visible'], (prefix, 'additive ordering/depth writes')
	assert frames['blend-0-visible'][1] == (51, 51, 51), (prefix, 'additive color')
	assert frames['blend-0-occluded'] == frames['blend-1-occluded'], (prefix, 'opaque occlusion')
	assert frames['blend-0-occluded'][1] == (0, 0, 51), (prefix, 'opaque depth test')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path, default=Path('build/checks/unavailable-render'))
	parser.add_argument('--capture-only', action='store_true', help='only the ten captured car/track pairs')
	parser.add_argument('--direct3d', action='store_true', help='test prepared Direct3D cars in the ten captured pairs')
	args = parser.parse_args()
	args.output.mkdir(parents=True, exist_ok=True)
	tool = (build_executable(args.build, 'mh-unavailable-render')).resolve()
	pairs = captures = 0

	for car in range(1, 11):
		for track in range(1, 9):
			capture = track == 1+(car-1)%8

			if (args.capture_only or args.direct3d) and not capture:
				continue

			command = [str(tool), str(args.gamedir.resolve()), str(car), str(track)]
			prefix = args.output/f'car{car:02d}-track{track}'

			if capture:
				command.append(str(prefix.resolve()))

				if args.direct3d:
					command.append('--direct3d')

			result = subprocess.run(command, capture_output=True, text=True, timeout=90)
			(args.output/f'car{car:02d}-track{track}.log').write_text(result.stdout+result.stderr)
			assert result.returncode == 0, (car, track, result.stdout, result.stderr)
			pairs += 1

			if capture:
				check_capture(prefix)
				captures += 1

		print(f'Car {car}: original planes/material bounds and native composition passed', flush=True)

	print(f'{pairs} car/track pairs, {captures} Vulkan capture sets: restoration, additive ordering, '
		'no depth writes and opaque occlusion passed')


if __name__ == '__main__':
	main()
