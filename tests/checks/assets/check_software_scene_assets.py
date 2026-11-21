#!/usr/bin/env python3
"""Check profile-specific race resources, full fields and late-load cleanup."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	subprocess.run([str((build_executable(args.build, 'mh-software-scene-assets')).resolve()),
		str(args.gamedir.resolve())], check=True, timeout=120)


if __name__ == '__main__':
	main()
