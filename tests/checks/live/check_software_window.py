#!/usr/bin/env python3
"""Check software window presentation under X11 (run with xvfb-run)."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	args = parser.parse_args()
	environment = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_RENDER_DRIVER='software')

	with tempfile.TemporaryDirectory(prefix='mh-software-window-') as directory:
		subprocess.run([str((build_executable(args.build, 'mh-software-window')).resolve()),
			str(Path(directory) / 'frame.bmp')],
			env=environment, check=True, timeout=30)


if __name__ == '__main__':
	main()
