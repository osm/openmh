#!/usr/bin/env python3
"""Verify executable-local content and --assets using a copied client."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from tests.checks.support.build import build_executable


from tests.checks.support.installation import prepare_view


def launch(binary, cwd, capture, override=None):
	command = [str(binary), '--renderer', 'software', '--audio', '0',
		'--debug-no-settings', '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-frames', '3',
		'--debug-capture', str(capture)]

	if override is not None:
		command += ['--assets', str(override)]

	result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
		timeout=60, env={**os.environ, 'SDL_VIDEO_DRIVER': 'x11'})
	assert result.returncode == 0, result.stdout + result.stderr
	assert capture.read_bytes()[:2] == b'BM', 'missing menu capture'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path,
		required=True)
	args = parser.parse_args()
	source = args.gamedir.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-game-directory-') as temporary:
		root = Path(temporary)
		installed = root / 'installation with spaces'
		alternate = root / 'alternate'
		elsewhere = root / 'unrelated working directory'
		prepare_view(installed, source)
		prepare_view(alternate, source)
		elsewhere.mkdir()
		binary = installed / 'openmotor'
		shutil.copy2(build_executable(args.build.resolve(), 'openmotor'), binary)
		launch(binary, installed, root / 'local.bmp')
		launch(binary, elsewhere, root / 'elsewhere.bmp')

		# Removing the default executable proves that the explicit override wins.
		(installed / 'motor.exe').unlink()
		launch(binary, elsewhere, root / 'override.bmp', alternate)

	print('Copied client: local launch, unrelated working directory and --assets passed.')


if __name__ == '__main__':
	main()
