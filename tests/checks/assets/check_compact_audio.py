#!/usr/bin/env python3
"""Qualify Opus playback with synthetic audio and an independent FFmpeg decode."""

import argparse
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave
import zipfile


def run(*arguments):
	subprocess.run(arguments, check=True, timeout=60)


def make_wave(path):
	samples = bytearray()

	for frame in range(44100 * 2):
		left = round(12000 * math.sin(2 * math.pi * 440 * frame / 44100))
		right = round(9000 * math.sin(2 * math.pi * 660 * frame / 44100))
		samples.extend(struct.pack('<hh', left, right))

	with wave.open(str(path), 'wb') as output:
		output.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
		output.writeframes(samples)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--decoder', type=Path, required=True)
	parser.add_argument('--ffmpeg', required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-compact-audio-') as temporary:
		root = Path(temporary)
		music = root / 'Music'
		music.mkdir()
		original = music / 'track-03.wav'
		encoded = music / 'track-02.opus'
		reference = root / 'reference.pcm'
		make_wave(original)
		run(args.ffmpeg, '-v', 'error', '-i', str(original), '-c:a', 'libopus',
			'-b:a', '160k', str(encoded))
		run(args.ffmpeg, '-v', 'error', '-i', str(encoded), '-f', 's16le',
			'-c:a', 'pcm_s16le', str(reference))
		shutil.copyfile(original, music / 'track-04.wav')
		shutil.copyfile(encoded, music / 'track-04.opus')
		package = root / 'test.ass'

		with zipfile.ZipFile(package, 'w', compression=zipfile.ZIP_STORED) as archive:
			archive.writestr('Music/', b'')

			for path in sorted(music.iterdir()):
				archive.write(path, path.relative_to(root))

		run(str(args.decoder.resolve()), str(root), str(package), str(reference))


if __name__ == '__main__':
	main()
