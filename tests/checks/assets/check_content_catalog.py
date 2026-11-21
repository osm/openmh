#!/usr/bin/env python3
"""Exercise appended car/track definitions without modifying original assets."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from tests.checks.support.tbl import reference_decode, reference_key


def encode_table(text):
	first = reference_key(bytes([1, 3, 250, 21, 32, 250, 42, 128, 170]), 233)
	second = reference_key(b"EQ", 227)
	payload = bytearray(text)
	total = 0

	for index in range(len(payload) - 1, -1, -1):
		byte = payload[index]
		payload[index] = (byte + total) & 255
		total = (total + byte) & 255

	total = 0

	for index, byte in enumerate(payload):
		payload[index] = (byte + total + first[index % 233] + second[index % 227]) & 255
		total = (total + byte) & 255

	return b"TBL\x05" + payload


def run(command, environment):
	result = subprocess.run([str(arg) for arg in command], env=environment,
		capture_output=True, text=True, timeout=120)

	if result.returncode:
		raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")

	return result.stdout


def prepare(source, destination):
	# Menu storage rejects symlinked files. Keep a private, ordinary tree so
	# storage reads and any generated progress cannot reach the originals.
	shutil.copytree(source, destination)
	game = next(path for path in destination.iterdir() if path.name.lower() == "game")
	car = next(path for path in game.iterdir() if path.name.lower() == "car01.car")
	track = next(path for path in game.iterdir() if path.name.lower() == "track1.trk")

	for original, target, key, name in (
		(car, game / "car11.car", b"CarName", b"CatalogCar"),
		(track, game / "Track9.trk", b"TrackName", b"CatalogTrack")):
		text = reference_decode(original.read_bytes())
		text, replacements = re.subn(rb"(?im)^" + key + rb"[ \t]+[^\r\n]*",
			key + b" " + name, text)

		if replacements != 1:
			raise ValueError(f"missing unique {key!r} in {original}")

		target.write_bytes(encode_table(text))

	# Track9 deliberately has no texture directory: its BasePath names Track1.


def check_named_track_attract(build, assets, environment):
	game = next(path for path in assets.iterdir() if path.name.lower() == 'game')
	track = next(path for path in game.iterdir() if path.name.lower() == 'track1.trk')
	text = reference_decode(track.read_bytes())
	text = re.sub(rb'(?im)^TrackName[ \t]+[^\r\n]*', b'TrackName CoastalRoadExtended', text)
	text = re.sub(rb'(?im)^Division[ \t]+[^\r\n]*', b'Division 100001', text)
	(game / 'track100-coast.trk').write_bytes(encode_table(text))
	run([build_executable(build, 'mh-attract-live'), assets], environment)
	print('Named add-on: numeric filename prefix and long name preserve stock attract races',
		flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path("build/linux"))
	parser.add_argument("--gamedir", type=Path, required=True)
	args = parser.parse_args()
	build = args.build.resolve()
	environment = dict(os.environ, SDL_AUDIO_DRIVER="dummy")

	with tempfile.TemporaryDirectory(prefix="mh-content-catalog-") as temporary:
		directory = Path(temporary)
		assets = directory / "assets"
		prepare(args.gamedir.resolve(), assets)

		for renderer in ("glide", "software"):
			common = ["--assets", assets, "--renderer", renderer,
				'--debug-car', "11", '--debug-track', "9", '--debug-no-settings', '--debug-no-records']
			output = run([build_executable(build, 'openmotor'), '--debug-race', *common, '--debug-opponents', "7",
				'--debug-ticks', "120", '--debug-ai', "2", '--debug-mute', '--debug-check',
				'--debug-window-titles', '1'], environment)

			if "CatalogCar, track 9" not in output:
				raise AssertionError(output)

			race_capture = directory / f"{renderer}-race.bmp"
			recording = directory / f"{renderer}.mde"
			capture_output = run([build_executable(build, 'openmotor'), '--debug-race', *common, '--debug-opponents', "7", '--debug-ticks', "3600",
				'--debug-ai', "2", '--debug-mute', '--debug-record-file', recording,
				'--debug-capture', race_capture, '--debug-window-titles', '1'], environment)

			if not race_capture.is_file() or race_capture.stat().st_size <= 54:
				raise AssertionError(f"missing race capture: {race_capture}")

			if not recording.is_file():
				raise AssertionError(f"missing custom-content recording: {recording}\n{capture_output}")

			run([build_executable(build, 'openmotor'), '--debug-demo', recording, "--assets", assets,
				'--debug-ticks', "120", '--debug-check', '--debug-no-settings', '--debug-no-records',
				'--debug-mute', '--debug-window-titles', '1'], environment)

			for page in ("car", "setup", "single", "sound", "rankings"):
				capture = directory / f"{renderer}-{page}.bmp"
				run([build_executable(build, "openmotor"), "--assets", assets,
					"--renderer", renderer, "--debug-car", "11", "--debug-track", "9",
					"--debug-no-settings", "--debug-no-records", "--debug-no-intro", "--audio", "0",
					"--debug-unlocked-cars", "11", "--debug-unlocked-tracks", "9",
					"--debug-page", page, "--debug-capture", capture, '--debug-window-titles', '1'], environment)

				if not capture.is_file() or capture.stat().st_size <= 54:
					raise AssertionError(f"missing menu capture: {capture}")

			print(f"{renderer}: car 11/track 9 with seven opponents, race capture, demo playback and five menu pages passed",
				flush=True)

		check_named_track_attract(build, assets, environment)


if __name__ == "__main__":
	main()
