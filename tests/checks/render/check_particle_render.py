#!/usr/bin/env python3
"""Check real braking particles, field lifecycle and native Vulkan composition."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess

from tests.checks.render.check_unavailable_render import bmp


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path, default=Path('build/checks/particles-render'))
	parser.add_argument('--capture-only', action='store_true')
	args = parser.parse_args()
	args.output.mkdir(parents=True, exist_ok=True)
	pairs = captures = 0

	for car in range(1, 11):
		for track in range(1, 9):
			capture = track == 1+(car-1)%8

			if args.capture_only and not capture:
				continue

			prefix = args.output/f'car{car:02d}-track{track}'
			command = [str((build_executable(args.build, 'mh-particle-render')).resolve()), str(args.gamedir.resolve()), str(car), str(track)]

			if capture:
				command.append(str(prefix.resolve()))

			result = subprocess.run(command, capture_output=True, text=True, timeout=90)
			prefix.with_suffix('.log').write_text(result.stdout+result.stderr)
			assert result.returncode == 0, (car, track, result.stdout, result.stderr)
			pairs += 1

			if capture:
				frames = {name:bmp(Path(str(prefix)+'-'+name+'.bmp')) for name in (
					'clean', 'smoke', 'effects', 'blend-base', 'blend-one', 'blend-two',
					'blend-ordered', 'blend-shifted', 'blend-occluded')}
				assert frames['clean'][0] != frames['smoke'][0], (prefix, 'missing braking particles')
				assert frames['clean'][0] != frames['effects'][0], (prefix, 'missing received effects')
				assert frames['blend-base'][1] == (64, 48, 32), prefix  # BMP BGR
				assert frames['blend-one'][1] == (160, 80, 96), (prefix, 'texture modulation/addition')
				assert frames['blend-two'][1] == (255, 112, 160), (prefix, 'saturation/no depth writes')
				assert frames['blend-ordered'][1] == (255, 120, 160), (prefix, 'opaque/mark/particle/unavailable order')
				assert frames['blend-one'] == frames['blend-shifted'], (prefix, 'preprojected particle matrix')
				assert frames['blend-base'] == frames['blend-occluded'], (prefix, 'opaque depth')
				captures += 1

		print(f'Car {car}: real braking, cadence and pool lifecycle passed', flush=True)

	print(f'{pairs} car/track pairs, {captures} Vulkan capture sets; textured addition, depth, pass order and NDC placement passed')


if __name__ == '__main__':
	main()
