#!/usr/bin/env python3
"""Load original catalogs, scenes, HUD/results and audio through native and prepared-memory providers."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	subprocess.run([str((build_executable(args.build, 'mh-resource-loaders')).resolve()),
		str(args.gamedir.resolve())], check=True, timeout=120)


if __name__ == '__main__':
	main()
