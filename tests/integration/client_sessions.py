#!/usr/bin/env python3
"""Check debug-session CLI dispatch without loading game assets or a display."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('client', type=Path)
	parser.add_argument('--runner', action='append', default=[],
		help='Command prefix argument; repeat for emulator options')
	parser.add_argument('--league', action='store_true')
	parser.add_argument('--network', action='store_true')
	args = parser.parse_args()

	def run(*options):
		return subprocess.run([*args.runner, str(args.client), *options], text=True,
			capture_output=True, timeout=20)

	for mode, available in (('race', True), ('demo', True),
		('league', args.league), ('network', args.network)):
		option = '--debug-' + mode
		result = run(option, '--help')
		output = result.stdout + result.stderr

		if not available:
			assert result.returncode != 0 and 'unavailable' in output, output
			continue

		assert result.returncode == 0, output
		assert 'openmotor ' + option in output, output
		flags = set(re.findall(r'--[a-z][a-z0-9-]*', output))
		assert all(flag.startswith('--debug-') or flag in
			{'--help', '--assets', '--renderer', '--network-protocol'} for flag in flags), flags
		assert run(option, '--unknown-session-option').returncode != 0

	assert run('--debug-demo').returncode != 0
	assert run('--debug-race', '--debug-demo', 'unused.MDE').returncode != 0
	assert run('--debug-race', '--car', '1').returncode != 0
	assert run('--debug-race', '--debug-gpu', 'yes').returncode != 0
	assert run('--debug-race', '--debug-texture-bytes').returncode != 0
	print('Debug session selection, help, unavailable modes and invalid arguments pass')


if __name__ == '__main__':
	main()
