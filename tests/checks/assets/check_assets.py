#!/usr/bin/env python3
"""Run native geometry/image validation on the extracted asset corpus."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path('build/linux'))
	parser.add_argument("--gamedir", type=Path, required=True)
	args = parser.parse_args()
	paths = sorted(p for p in args.gamedir.rglob("*")
		if p.suffix.lower() in {".myo", ".myw", ".pdi", ".iff"})

	if not paths:
		parser.error(f"no assets found under {args.gamedir}")

	placeholder = args.gamedir / "Data/d3d/tullinge.iff"

	if placeholder in paths and not placeholder.read_bytes().startswith(b"FORM"):
		paths.remove(placeholder)
		print(f"Skipping known non-image placeholder: {placeholder}", flush=True)

	result = subprocess.call([str((build_executable(args.build, "mh-asset-check")).resolve()),
		*map(str, paths)])

	if result:
		return result

	geometry = [p for p in paths if p.suffix.lower() in {".myo", ".myw"}]

	if not geometry:
		return 0

	return subprocess.call([str((build_executable(args.build, "mh-model-data-check")).resolve()),
		*map(str, geometry)])


if __name__ == "__main__":
	raise SystemExit(main())
