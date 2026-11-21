#!/usr/bin/env python3
"""Check indexed decoding and offscreen software submission with track assets."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	executable = (build_executable(args.build, 'mh-software-indexed')).resolve()
	images = 0

	for track in range(1, 9):
		for name in ('tex1.pdi', 'tex2.pdi'):
			archive = args.gamedir / 'Tracks' / f'track{track}' / 'Packed' / name
			result = subprocess.run([str(executable), str(archive)],
				capture_output=True, text=True, timeout=60)

			if result.returncode:
				raise RuntimeError(f'{archive}: {result.stdout}{result.stderr}')

			images += int(result.stdout.split()[0])
			print(f'{archive}: {result.stdout.strip()}', flush=True)

	print(f'{images} indexed images checked across 16 archives; '
		'32 opaque/cutout textured mesh submissions passed')


if __name__ == '__main__':
	main()
