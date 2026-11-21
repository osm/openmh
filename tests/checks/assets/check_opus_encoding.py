#!/usr/bin/env python3
"""Check library Opus encoding, playback, sample counts and cancellation."""

import argparse
import array
import math
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import wave


def run(*args):
	result = subprocess.run(args, capture_output=True, text=True, timeout=60)

	if result.returncode == 77:
		print(result.stderr, file=sys.stderr)

		raise SystemExit(77)

	assert result.returncode == 0, (args, result.stderr)

	return result.stdout


def check_case(encoder, ffmpeg, root, rate, channels, bits, frames):
	name = f'{rate}-{channels}-{bits}-{frames}'
	source, encoded = root / f'{name}.wav', root / f'{name}.opus'
	samples = [int(12000 * math.sin(2 * math.pi * 440 * i / rate))
		for i in range(frames) for _ in range(channels)]

	if bits == 8:
		pcm = bytes((sample // 256) + 128 for sample in samples)
	else:
		pcm = array.array('h', samples)

		if sys.byteorder != 'little':
			pcm.byteswap()

		pcm = pcm.tobytes()

	with wave.open(str(source), 'wb') as output:
		output.setparams((channels, bits // 8, rate, frames, 'NONE', 'not compressed'))
		output.writeframes(pcm)

	if len(pcm) % 2:
		# Python's wave writer omits RIFF's padding for odd data chunks.
		padded = bytearray(source.read_bytes()) + b'\0'
		struct.pack_into('<I', padded, 4, len(padded) - 8)
		source.write_bytes(padded)

	fields = run(str(encoder), str(source), str(encoded)).split()
	expected_frames = (frames * 48000 + rate - 1) // rate
	assert fields == [str(expected_frames), '48000', str(channels)], (name, fields)
	decoded = root / f'{name}.pcm'
	run(ffmpeg, '-v', 'error', '-i', str(encoded), '-f', 's16le', str(decoded))
	values = array.array('h', decoded.read_bytes())

	if sys.byteorder != 'little':
		values.byteswap()

	assert len(values) == expected_frames * channels, name

	# Ignore only the resampling filter's edges for this signal-quality check.
	# Duration and decoder trimming are checked across the entire stream above.
	errors = [(values[i * channels] - 12000 * math.sin(2 * math.pi * 440 * i / 48000)) ** 2
		for i in range(100, expected_frames - 100)]
	rms = math.sqrt(sum(errors) / len(errors))
	assert rms < 300, (name, 'signal error', rms)

	if rate == 44100 and channels == 2 and bits == 16:
		assert encoded.stat().st_size < source.stat().st_size / 3, name

	cancelled = root / 'cancelled.opus'
	run(str(encoder), str(source), str(cancelled), 'cancel')
	assert not cancelled.exists(), 'cancelled conversion published output'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--encoder', required=True, type=Path)
	parser.add_argument('--ffmpeg', required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-opus-encoding-') as temporary:
		root = Path(temporary)

		for case in ((44100, 2, 16, 88323), (48000, 1, 16, 48713),
			(22050, 1, 8, 33457)):
			check_case(args.encoder.resolve(), args.ffmpeg, root, *case)

	print('Opus encoding, playback, resampling, final trimming and cancellation passed')


if __name__ == '__main__':
	main()
