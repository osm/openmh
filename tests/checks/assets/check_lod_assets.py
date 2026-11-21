#!/usr/bin/env python3
"""Qualify body detail packing, native drawing and optional chain loading."""

from tests.checks.support.build import build_executable
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

from tests.checks.render.check_unavailable_render import bmp


def captures(prefix):
	frames = {}

	for level in range(4):
		for effect in range(2):
			packed = bmp(Path(f'{prefix}-packed-{level}-{effect}.bmp'))[0]
			single = bmp(Path(f'{prefix}-single-{level}-{effect}.bmp'))[0]
			assert packed == single, (prefix, level, effect, 'packed/standalone pixels')
			assert len(set(packed)) > 4, (prefix, level, effect, 'empty capture')
			frames[level, effect] = packed

		assert frames[level, 0] != frames[level, 1], (prefix, level, 'material transition')

	assert len({frames[level, 0] for level in range(4)}) > 1, (prefix, 'detail never changed')

	for effect in range(2):
		assert bmp(Path(f'{prefix}-restored-0-{effect}.bmp'))[0] == frames[0, effect], (prefix, 'restoration')


def chain_loading(tool, assets):
	# Only this disposable directory is modified. All unrelated resources
	# are read through symlinks; no original asset is renamed or overwritten.
	source = assets/'Cars/car01'
	originals = [source/f'KOBRAX3_OBJ{level}.MYO' for level in range(4)]
	hashes = [hashlib.sha256(path.read_bytes()).digest() for path in originals]
	cases = [(set(range(levels)), None, None) for levels in range(1, 5)]
	cases += [(set(), None, 'missing base car model'),
		({0, 2, 3}, None, 'non-contiguous car detail chain'),
		({0, 1, 3}, None, 'non-contiguous car detail chain'),
		({0, 1, 2}, 2, 'MYO header'),
		({0, 1}, 'ambiguous', 'ambiguous filename'),
		({0, 1, 2, 3}, 'missing-wheel', 'missing wheel animation variant')]

	for present, bad, expected in cases:
		with tempfile.TemporaryDirectory(prefix='mh-detail-chain-') as temp:
			root = Path(temp)
			cars = root/'Cars'
			car = cars/'car01'
			car.mkdir(parents=True)

			for parent, target, excluded in [(assets, root, 'Cars'), (assets/'Cars', cars, 'car01')]:
				for path in parent.iterdir():
					if path.name != excluded:
						(target/path.name).symlink_to(path, target_is_directory=path.is_dir())

			for path in source.iterdir():
				if path not in originals:
					(car/path.name).symlink_to(path, target_is_directory=path.is_dir())

			for level in present:
				target = car/originals[level].name

				if bad == level:
					target.write_bytes(b'broken model')
				else:
					shutil.copyfile(originals[level], target)

			if bad == 'ambiguous':
				shutil.copyfile(originals[0], car/originals[0].name.lower())

			if bad == 'missing-wheel':
				(car/'KOBRAX3_HF_OBJ3.MYO').unlink()

			command = [str(tool), str(root), '1', '1', '--levels', str(max(1, len(present)))]
			result = subprocess.run(command, text=True, capture_output=True, timeout=90)

			if expected:
				assert result.returncode == 1 and expected in result.stderr, (present, bad, result.stderr)
			else:
				assert result.returncode == 0, (present, result.stderr)

	assert hashes == [hashlib.sha256(path.read_bytes()).digest() for path in originals]
	print(f'{len(cases)} chain fixtures: optional tails, missing base/wheel, holes, malformed and ambiguous files passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path, default=Path('build/checks/model-detail'))
	parser.add_argument('--capture-only', action='store_true')
	args = parser.parse_args()
	args.output.mkdir(parents=True, exist_ok=True)
	assets = args.gamedir.resolve()
	tool = (build_executable(args.build, 'mh-lod-assets')).resolve()
	chain_loading(tool, assets)
	pairs = captured = 0

	for car in range(1, 11):
		for track in range(1, 9):
			capture = track == 1+(car-1)%8

			if args.capture_only and not capture:
				continue

			prefix = (args.output/f'car{car:02d}-track{track}').resolve()
			command = [str(tool), str(assets), str(car), str(track)]

			if capture:
				command.append(str(prefix))

			result = subprocess.run(command, text=True, capture_output=True, timeout=90)
			prefix.with_suffix('.log').write_text(result.stdout+result.stderr)
			assert result.returncode == 0, (car, track, result.stderr)
			halos = subprocess.run([str((build_executable(args.build, 'mh-attached-halo-assets')).resolve()),
				str(assets), str(car), str(track)], text=True, capture_output=True, timeout=90)
			Path(f'{prefix}-halos.log').write_text(halos.stdout+halos.stderr)
			assert halos.returncode == 0, (car, track, halos.stderr)
			pairs += 1

			if capture:
				captures(prefix)
				captured += 1

		print(f'Car {car}: geometry, materials, images, detail policy and attached halos passed', flush=True)

	print(f'{pairs} car/track pairs, {captured} Vulkan capture sets; packed/standalone pixels and restoration match')


if __name__ == '__main__':
	main()
