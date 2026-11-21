#!/usr/bin/env python3
"""Compare packed animated scenery with qualified standalone model draws.

Asset paths are resolved here so the C fixture stays independent of SDL and
filesystem directory services.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess

from tests.checks.assets.check_scenes import read_scene


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	maps = {p.stem.lower(): p for p in (args.gamedir / 'Data').iterdir()
		if p.suffix.lower() == '.iff'}
	names = ('env_city', 'env_nolby', 'env_city', 'env_atl', 'envphong_g5',
		'env_oly', 'env_city', 'env_nolby')
	instances = 0

	for number in range(1, 9):
		track = args.gamedir / 'Tracks' / f'track{number}'
		world, = [p for p in track.iterdir() if p.suffix.lower() == '.myw']
		scene, = track.glob('*.lws')
		_, objects = read_scene(scene.read_bytes())
		models = {p.name.lower(): p for p in (track / 'Objects').iterdir()}
		unique = dict.fromkeys(name for name, _, _, _ in objects)
		paths = ''.join(str(models[name.lower()]) + '\n' for name in unique)
		command = [str(build_executable(args.build, 'mh-software-scenery')), str(world),
			str(track / 'Packed/tex1.pdi'), str(scene),
			str(maps[names[number - 1]]),
			str(args.gamedir / 'Game' / f'Track{number}.trk')]
		result = subprocess.run(command, input=paths, text=True,
			capture_output=True, timeout=120)
		assert result.returncode == 0, (number, result.stdout, result.stderr)
		print(f'Track {number}: {result.stdout.strip()}', flush=True)
		instances += len(objects)

	print(f'{instances} animated scenery instances match separate draws across '
		'eight tracks; mixed world/scenery frames also render')


if __name__ == '__main__':
	main()
