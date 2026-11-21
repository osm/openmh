"""Launch each application from a native release in a directory of its own."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def check(archive, linux_image=None):
	from tools.native_archive import APPLICATIONS, check_archive

	windows = archive.suffix == '.zip'

	if windows and linux_image:
		raise ValueError('--linux-image requires a Linux release archive')

	root = archive.name.removesuffix('.zip' if windows else '.tar.gz')
	programs = {name + ('.exe' if windows else '') for name in APPLICATIONS}
	check_archive(archive, programs, root, allow_libraries=False)
	runner = []

	if windows and os.name != 'nt':
		wine = shutil.which('wine') or shutil.which('wine64')

		if not wine:
			raise ValueError('Install Wine to run the Windows release on this host')

		runner.append(wine)

	with tempfile.TemporaryDirectory() as temporary:
		directory = Path(temporary)
		shutil.unpack_archive(archive, directory)

		for name in sorted(programs):
			isolated = directory / name.removesuffix('.exe')
			isolated.mkdir()
			program = (directory / root / name).rename(isolated / name)
			command = [*runner, str(program), '--help']

			if linux_image:
				command = ['docker', 'run', '--rm', '--platform', 'linux/amd64',
					'--network', 'none', '--read-only', '-v', f'{isolated}:/app:ro',
					'-w', '/app', linux_image, './' + name, '--help']

			result = subprocess.run(command,
				cwd=isolated, capture_output=True, text=True, timeout=90)

			if result.returncode or f'Usage: {program.stem} ' not in result.stdout:
				raise ValueError(f'{name} failed to launch: {result.stdout}{result.stderr}')

			print(f'{name} started on its own')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('archive', type=Path)
	parser.add_argument('--linux-image', help='launch Linux executables in this Docker image')
	args = parser.parse_args()
	check(args.archive.resolve(), args.linux_image)


if __name__ == '__main__':
	main()
