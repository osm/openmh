#!/usr/bin/env python3
"""Compare complete software cars with independently loaded detail models.

Exercises the portable scene's transforms, detail/visibility decisions and
lighting, with raw track textures and all three authored environment slots.
This compares integration across scene and standalone model drawing.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess

from tests.checks.support.tbl import reference_decode


def fields(path):
	result = {}

	for line in reference_decode(path.read_bytes()).decode('ascii').splitlines():
		words = line.split('//', 1)[0].split()

		if words:
			result.setdefault(words[0].lower(), []).append(words[1:])

	return result


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--car', type=int, choices=range(1, 11))
	parser.add_argument('--track', type=int, choices=range(1, 9))
	parser.add_argument('--output', type=Path,
		help='Optional representative TGA for each car/track pair')
	args = parser.parse_args()
	files = {str(p.relative_to(args.gamedir)).lower(): p
		for p in args.gamedir.rglob('*') if p.is_file()}

	def asset(name):
		return files[name.replace('\\', '/').lower()]

	if args.output:
		args.output.mkdir(parents=True, exist_ok=True)

	pairs = 0

	for car in ([args.car] if args.car else range(1, 11)):
		config = asset(f'Game/car{car:02}.car')
		data = fields(config)
		stems = [data['filename'][0][0]]
		stems += [data[f'wheel{i}'][0][0] for i in range(1, 5)]
		paths = ''.join(str(asset(f'{stem}{level}.myo')) + '\n'
			for stem in stems for level in range(4))

		for track in ([args.track] if args.track else range(1, 9)):
			track_config = asset(f'Game/Track{track}.trk')
			settings = fields(track_config)
			world = asset(settings['basepath'][0][0] + '/' + settings['filename'][0][0])
			maps = [asset(row[0]) for row in settings['envmapname']]
			assert len(maps) == 3
			command = [str(build_executable(args.build, 'mh-software-cars')), str(config),
				str(track_config), str(world),
				str(asset(f'Tracks/track{track}/Packed/tex1.pdi')),
				*map(str, maps)]

			if args.output:
				command.append(str(args.output / f'car{car:02}-track{track}.tga'))

			result = subprocess.run(command, input=paths, text=True,
				capture_output=True, timeout=120)
			assert result.returncode == 0, (car, track, result.stdout, result.stderr)
			pairs += 1

		print(f'Car {car}: body/wheels, three qualities, shading, animation, '
			'detail and visibility passed', flush=True)

	print(f'{pairs} car/track pairs: {pairs * 60} assembled frames match '
		f'independent detail models and explicitly selected lights; {pairs * 4} '
		'mixed world/car frames also match')


if __name__ == '__main__':
	main()
