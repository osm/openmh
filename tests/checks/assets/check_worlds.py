#!/usr/bin/env python3
"""Compare native world mesh totals with independent byte walks of all MYWs."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import struct
import subprocess


from tests.checks.support.process import COMMAND_TIMEOUT


def counts(path):
	data = path.read_bytes()
	grid = struct.unpack_from("<II", data, 8)
	faces, length, cursor = struct.unpack_from("<III", data, 0x80)
	end = cursor + length
	sizes = (16, 20, 24, 24, 24, 32, 32, 36, 20, 24)
	triangles = 0

	for _ in range(faces):
		kind = data[cursor]
		triangles += 1 + (kind & 1)
		cursor += sizes[kind]

	if cursor != end:
		raise ValueError(f"primitive span mismatch: {path}")

	textures = struct.unpack_from("<I", data, 0xd4)[0]

	return grid, faces, triangles, textures + 1


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path('build/linux'))
	parser.add_argument("--gamedir", type=Path, required=True)
	args = parser.parse_args()
	viewer = str((build_executable(args.build, "mh-model-viewer")).resolve())
	total = 0

	for number in range(1, 9):
		base = args.gamedir / "Tracks" / f"track{number}"

		for hardware in (False, True):
			directory = base / "Hardware" if hardware else base
			paths = [p for p in directory.iterdir() if p.suffix.lower() == ".myw"]

			if len(paths) != 1:
				raise ValueError(f"expected one world under {directory}")

			grid, faces, triangles, images = counts(paths[0])
			command = [viewer, "--gamedir", str(args.gamedir), "--world", "--no-scenery",
				"--track", str(number), "--check"]

			if not hardware:
				command.append("--standard-world")

			result = subprocess.run(command, check=True, capture_output=True, text=True, timeout=COMMAND_TIMEOUT)

			if (f"{grid[0]} x {grid[1]} cells, {faces} faces;" not in result.stdout
				or f"{triangles} triangles, {images} image slots" not in result.stdout):
				raise ValueError(f"native/reference mismatch: {paths[0]}\n{result.stdout}")

			total += faces

		print(f"Track {number}: standard/hardware geometry and shared textures passed", flush=True)

	print(f"All 16 native world meshes match reference totals: {total} faces")


if __name__ == "__main__":
	main()
