#!/usr/bin/env python3
"""Exercise software race frames, pause and resident results on all eight tracks."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--capture', type=Path, help='Save the track 1 frame as TGA')
	parser.add_argument('--hidden', action='store_true',
		help='Run six weather/hidden modes on track 1 instead')
	parser.add_argument('--finish', action='store_true',
		help='Render complete physical races and held results on all tracks')
	args = parser.parse_args()

	if sum((args.hidden, args.finish, bool(args.capture))) > 1:
		parser.error('--hidden, --finish and --capture are mutually exclusive')

	command = [str((build_executable(args.build, 'mh-software-race')).resolve()),
		str(args.gamedir.resolve())]

	if args.capture:
		command.append(str(args.capture.resolve()))

	if args.hidden:
		command.append('--hidden')

	if args.finish:
		command.append('--finish')

	env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
	subprocess.run(command, env=env, check=True, timeout=3600 if args.finish else 600)


if __name__ == '__main__':
	main()
