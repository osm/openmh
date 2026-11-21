#!/usr/bin/env python3
"""Compare native and Wasm physics at every tick using the same real assets.

Build mh-simulation-timing-check in both builds first. The fixture replays
timestamped controls, an AI opponent, landings, impacts and recovery at
60/120/240 Hz. Each build also checks identical states across render cadences
from 30 to 1001 FPS and a schedule with jitter and stalls. No window or saves.
"""

import argparse
import csv
from itertools import zip_longest
import json
import math
from pathlib import Path
import subprocess

from tests.checks.support.build import build_executable


RATES = (60, 120, 240)
SCENARIOS = ("acceleration", "braking", "steering", "airborne",
	"pair-impact", "recovery", "wall-impact")
VALUE_COUNT = 512


def expected_ticks():
	for shaped in range(2):
		for rate in RATES:
			for tick in range(1, 10 * rate + 1):
				yield ["driving", str(shaped), str(rate), str(tick)]

	for scenario in SCENARIOS:
		for rate in RATES:
			for tick in range(1, 2 * rate + 1):
				yield [scenario, "0", str(rate), str(tick)]


def compare_traces(native, wasm):
	count = 0

	with native.open(newline="") as native_file, wasm.open(newline="") as wasm_file:
		rows = zip_longest(expected_ticks(), csv.reader(native_file),
			csv.reader(wasm_file))

		for key, left, right in rows:
			if key is None or left is None or right is None:
				raise ValueError(f"Unexpected trace length after {count} ticks")

			label = f"{key[0]} shaped={key[1]} rate={key[2]} tick={key[3]}"

			for platform, row in (("native", left), ("Wasm", right)):
				if len(row) != VALUE_COUNT + 5 or row[:4] != key:
					raise ValueError(f"{platform}: malformed or missing {label}")

			for index, (a, b) in enumerate(zip(left[4:-1], right[4:-1])):
				a, b = float(a), float(b)

				if not math.isfinite(a) or not math.isfinite(b) or a != b:
					raise ValueError(
						f"{label} value[{index}]: native={a!r}, Wasm={b!r}")

			if left[-1] != right[-1]:
				raise ValueError(f"{label}: visited checkpoints differ")

			count += 1

	return count


def run_fixture(command, assets, output, name):
	trace = output / f"{name}.csv"
	log = output / f"{name}.log"

	with log.open("w") as stream:
		result = subprocess.run(
			[*command, str(assets), "--trace", str(trace)],
			stdout=stream, stderr=subprocess.STDOUT, timeout=180)

	if result.returncode:
		raise RuntimeError(f"{name} fixture failed; see {log}")

	return trace


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--native-build", type=Path, required=True)
	parser.add_argument("--wasm-build", type=Path, required=True)
	parser.add_argument("--node", required=True, help="Emscripten SDK Node executable")
	parser.add_argument("--assets", type=Path, required=True,
		help="read-only extracted retail installation")
	parser.add_argument("--output", type=Path, required=True,
		help="new output directory for traces and report")
	args = parser.parse_args()
	native = build_executable(args.native_build, "mh-simulation-timing-check")
	wasm = build_executable(args.wasm_build, "mh-simulation-timing-check")
	assets = args.assets.resolve(strict=True)
	output = args.output.resolve()
	output.mkdir(parents=True, exist_ok=False)
	report = {"passed": False, "assets": str(assets), "rates": list(RATES),
		"native": str(native), "wasm": str(wasm)}

	try:
		native_trace = run_fixture([str(native)], assets, output, "native")
		wasm_trace = run_fixture([args.node, str(wasm)], assets, output, "wasm")
		report["matching_ticks"] = compare_traces(native_trace, wasm_trace)
		report["passed"] = True
	except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as error:
		report["error"] = str(error)

		raise
	finally:
		(output / "report.json").write_text(json.dumps(report, indent=2) + "\n")

	print(f'{report["matching_ticks"]} ticks match exactly across native and Wasm; '
		'all render-cadence comparisons passed')


if __name__ == "__main__":
	main()
