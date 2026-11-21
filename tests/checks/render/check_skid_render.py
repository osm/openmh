#!/usr/bin/env python3
"""Verify actual braking, skid lifecycle and native Vulkan multiplication."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess

from tests.checks.render.check_unavailable_render import bmp


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path, default=Path('build/checks/skids-render'))
	parser.add_argument('--capture-only', action='store_true')
	parser.add_argument('--direct3d', action='store_true',
		help='capture prepared Direct3D marks; retain Glide blend regression')
	args = parser.parse_args()
	args.output.mkdir(parents=True, exist_ok=True)
	pairs = captures = 0

	for car in range(1, 11):
		for track in range(1, 9):
			capture = track == 1+(car-1)%8

			if (args.capture_only or args.direct3d) and not capture:
				continue

			prefix = args.output/f'car{car:02d}-track{track}'
			command = [str((build_executable(args.build, 'mh-skids-assets')).resolve()), str(args.gamedir.resolve()), str(car), str(track)]

			if capture:
				command.append(str(prefix.resolve()))

			if args.direct3d:
				command.append('--direct3d')

			result = subprocess.run(command, capture_output=True, text=True, timeout=90)
			prefix.with_suffix('.log').write_text(result.stdout+result.stderr)
			assert result.returncode == 0, (car, track, result.stdout, result.stderr)
			pairs += 1

			if capture:
				frames = {name:bmp(Path(str(prefix)+'-'+name+'.bmp')) for name in
					('clean', 'marked', 'blend-base', 'blend-one', 'blend-two', 'blend-occluded')}
				assert frames['clean'][0] != frames['marked'][0], (prefix, 'missing actual tyre marks')
				assert frames['blend-base'][1] == (128, 128, 128), prefix
				assert frames['blend-one'][1] == (108, 108, 108), (prefix, 'Glide multiplier')
				assert frames['blend-two'][1] == (91, 91, 91), (prefix, 'overlap must not write depth')
				assert frames['blend-base'] == frames['blend-occluded'], (prefix, 'opaque occlusion')
				captures += 1

		print(f'Car {car}: real braking/contact history and restart checks passed', flush=True)

	profile = 'Direct3D' if args.direct3d else 'Glide'
	print(f'{profile}: {pairs} car/track pairs, {captures} Vulkan capture sets; Glide multiplier/overlap/occlusion regression passed')


if __name__ == '__main__':
	main()
