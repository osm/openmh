#!/usr/bin/env python3
"""Run original menu/race assets and abort return through the SDK-free host."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess
import tempfile


def pack_resources(assets, output):
	files = sorted(path for path in assets.rglob('*') if path.is_file())
	assert 0 < len(files) <= 4096
	output.write(f'{len(files)}\n'.encode('ascii'))
	total = 0

	for path in files:
		name = 'memory-assets/' + path.relative_to(assets).as_posix()
		encoded = name.encode('ascii')
		assert b'\n' not in encoded and len(encoded) < 1023
		data = path.read_bytes()
		total += len(data)
		assert total <= 256 * 1024 * 1024
		output.write(encoded + b'\n')
		output.write(f'{len(data)}\n'.encode('ascii'))
		output.write(data)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-portable-host-') as directory:
		pack = Path(directory) / 'resources.pack'

		with pack.open('wb') as output:
			pack_resources(args.gamedir, output)

		subprocess.run([str(build_executable(args.build, 'test-portable-host')), str(pack)],
			check=True, timeout=240)


if __name__ == '__main__':
	main()
