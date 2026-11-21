#!/usr/bin/env python3
"""Validate TBL decoding and ten car assemblies with all eight track materials."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess


from tests.checks.support.tbl import reference_decode


from tests.checks.support.process import COMMAND_TIMEOUT


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path('build/linux'))
	parser.add_argument("--gamedir", type=Path, required=True)
	args = parser.parse_args()
	configs = sorted(path for path in (args.gamedir / "Game").iterdir()
		if path.suffix.lower() in {".car", ".trk"})

	if len(configs) != 18:
		parser.error(f"expected 18 car/track configurations, found {len(configs)}")

	environments = {}

	for path in configs:
		reference = reference_decode(path.read_bytes())
		actual = subprocess.check_output([str((build_executable(args.build, "mh-tbl-decode")).resolve()),
			str(path)], timeout=COMMAND_TIMEOUT)

		if actual != reference:
			raise ValueError(f"native/reference decode mismatch: {path}")

		if any(byte not in (9, 10, 13) and not 32 <= byte <= 126 for byte in actual):
			raise ValueError(f"non-ASCII decoded configuration: {path}")

		if path.suffix.lower() == ".trk":
			names = [line.split()[1] for line in reference.decode("ascii").splitlines()
				if line.split() and line.split()[0].lower() == "envmapname"]

			if len(names) != 3:
				raise ValueError(f"expected three environment maps: {path}")

			environments[int(path.stem[5:])] = names[0]

	print(f"{len(configs)} TBL configurations match the reference decoder", flush=True)

	for number in range(1, 11):
		for track in range(1, 9):
			result = subprocess.run([str((build_executable(args.build, "mh-model-viewer")).resolve()),
				"--gamedir", str(args.gamedir), "--car", str(number),
				"--track", str(track), "--check"],
				check=True, capture_output=True, text=True, timeout=COMMAND_TIMEOUT)

			if f"Track {track} environment: {environments[track]}\n" not in result.stdout:
				raise ValueError(f"environment selection mismatch: car {number}, track {track}")

		print(f"Car {number}: body and four wheels loaded with all eight track materials",
			flush=True)

	print("All 80 car/track material combinations passed")


if __name__ == "__main__":
	main()
