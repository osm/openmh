#!/usr/bin/env python3
"""Compare native initial scenery poses and mesh totals with independent reads."""

from tests.checks.support.build import build_executable
import argparse
import math
from pathlib import Path
import struct
import subprocess

from tests.checks.support.tbl import reference_decode
from tests.checks.assets.check_worlds import counts as world_counts


from tests.checks.support.process import COMMAND_TIMEOUT


def rotation(h, p, b):
	h, p, b = map(math.radians, (h, p, b))
	ch, sh, cp, sp, cb, sb = math.cos(h), math.sin(h), math.cos(p), math.sin(p), math.cos(b), math.sin(b)
	matrices = (((ch, 0, sh), (0, 1, 0), (-sh, 0, ch)),
		((1, 0, 0), (0, cp, -sp), (0, sp, cp)),
		((cb, -sb, 0), (sb, cb, 0), (0, 0, 1)))
	result = matrices[0]

	for matrix in matrices[1:]:
		result = [[sum(result[row][k] * matrix[k][col] for k in range(3))
			for col in range(3)] for row in range(3)]

	return [result[row][col] for col in range(3) for row in range(3)]


def read_scene(data):
	text = reference_decode(data) if data.startswith(b'TBL') else data
	lines = text.decode("ascii").splitlines()
	objects = []
	fps = 30

	for i, line in enumerate(lines):
		if line.startswith("FramesPerSecond "):
			fps = float(line.split()[1])

		if line.startswith("LoadObject "):
			name = line.split()[1].replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0].lower() + ".myo"

		if line.startswith("ObjectMotion "):
			assert int(lines[i + 1]) == 9
			count = int(lines[i + 2])
			values = list(map(float, lines[i + 3].split()))
			end = int(lines[i + 3 + count * 2].split()[1])
			objects.append((name, count, end, values[:3] + rotation(*values[3:6]) + values[6:]))

	return fps, objects


def model_counts(path):
	data = path.read_bytes()
	faces, size, cursor = struct.unpack_from("<III", data, 0x10)
	end = cursor + size
	triangles = 0
	sizes = (16, 20, 24, 24, 24, 32, 32, 36, 20, 24)

	for _ in range(faces):
		kind = data[cursor]
		triangles += 1 + (kind & 1)
		cursor += sizes[kind]

	assert cursor == end

	return triangles, struct.unpack_from("<I", data, 0x4c)[0] + 1


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path('build/linux'))
	parser.add_argument("--gamedir", type=Path, required=True)
	args = parser.parse_args()
	total = keys = 0

	for number in range(1, 9):
		base = args.gamedir / "Tracks" / f"track{number}"
		scene_path, = base.glob("*.lws")
		fps, objects = read_scene(scene_path.read_bytes())
		output = subprocess.check_output([str((build_executable(args.build, "mh-scene-info")).resolve()),
			str(scene_path)], text=True, timeout=COMMAND_TIMEOUT).splitlines()
		header = output.pop(0).split("\t")
		assert header == ["scene", str(len(objects)), str(sum(o[1] for o in objects)), f"{fps:g}"]
		assert len(output) == len(objects)

		for actual, (name, count, end, pose) in zip(output, objects):
			fields = actual.split("\t")
			assert fields[:3] == [name, str(count), str(end)]
			assert len(fields[3:]) == len(pose)
			assert all(math.isclose(float(a), b, rel_tol=1e-10, abs_tol=1e-10)
				for a, b in zip(fields[3:], pose)), (scene_path, name)

		models = {p.name.lower(): p for p in (base / "Objects").iterdir()}
		counts = {name: model_counts(models[name]) for name, *_ in objects}
		extra_triangles = sum(counts[name][0] for name, *_ in objects)
		extra_images = sum(images for _, images in counts.values()) + 1  # environment

		for hardware in (False, True):
			directory = base / "Hardware" if hardware else base
			world, = (p for p in directory.iterdir() if p.suffix.lower() == ".myw")
			_, _, triangles, images = world_counts(world)
			command = [str((build_executable(args.build, "mh-model-viewer")).resolve()), "--world", "--track", str(number),
				"--gamedir", str(args.gamedir), "--time", "7.5", "--check"]

			if not hardware:
				command.append("--standard-world")

			actual = subprocess.check_output(command, text=True, timeout=COMMAND_TIMEOUT)
			assert f"{triangles + extra_triangles} triangles, {images + extra_images} image slots" in actual

		total += len(objects)
		keys += sum(o[1] for o in objects)
		print(f"Track {number}: {len(objects)} poses, {len(counts)} models, +{extra_triangles} triangles; both worlds at 7.5s passed", flush=True)

	print(f"All {total} initial poses matched; {keys} motion keys validated; 16 scene assemblies passed")


if __name__ == "__main__":
	main()
