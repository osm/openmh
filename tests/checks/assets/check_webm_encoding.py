#!/usr/bin/env python3
"""Check library WebM encoding, frame/audio timing and cancellation."""

import argparse
import array
import math
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args):
	result = subprocess.run(args, capture_output=True, text=True, timeout=60)

	if result.returncode == 77:
		print(result.stderr, file=sys.stderr)

		raise SystemExit(77)

	assert result.returncode == 0, (args, result.stderr)

	return result.stdout


def check_clip(args, root, rate, sound):
	source, encoded = root / 'source.webm', root / 'encoded.webm'
	inputs = ['-f', 'lavfi', '-i', f'testsrc2=size=64x48:rate={rate}:duration=2']

	if sound:
		inputs += ['-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=2']

	run(args.ffmpeg, '-v', 'error', '-y', *inputs, '-c:v', 'libvpx-vp9',
		'-lossless', '1', '-auto-alt-ref', '0', '-lag-in-frames', '0',
		'-colorspace', 'smpte170m', '-c:a', 'libopus', '-b:a', '160k', '-ac', '2', str(source))
	run(str(args.encoder), str(source), str(encoded))
	reference_pixels, reference_audio = root / 'reference.rgba', root / 'reference.pcm'
	pixels, audio = root / 'encoded.rgba', root / 'encoded.pcm'
	original_info = run(str(args.decoder), str(source), str(reference_pixels), str(reference_audio), '1')
	encoded_info = run(str(args.decoder), str(encoded), str(pixels), str(audio), '1')
	assert original_info.split() == encoded_info.split(), (original_info, encoded_info)
	original, compressed = reference_pixels.read_bytes(), pixels.read_bytes()
	assert len(original) == len(compressed)
	rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(original, compressed)) / len(original))
	assert rms < 12, ('picture signal error', rms)
	a, b = array.array('h', reference_audio.read_bytes()), array.array('h', audio.read_bytes())
	assert len(a) == len(b), 'audio duration differs'

	if sys.byteorder != 'little':
		a.byteswap()
		b.byteswap()

	if sound:
		rms = math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))
		assert rms < 300, ('audio signal error', rms)

	cancelled = root / 'cancelled.webm'
	run(str(args.encoder), str(source), str(cancelled), 'cancel')
	assert not cancelled.exists(), 'cancelled encoding published output'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--encoder', required=True, type=Path)
	parser.add_argument('--decoder', required=True, type=Path)
	parser.add_argument('--ffmpeg', required=True)
	args = parser.parse_args()
	args.encoder, args.decoder = args.encoder.resolve(), args.decoder.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-webm-encoding-') as temporary:
		for rate, sound in ((25, True), (12.5, False)):
			check_clip(args, Path(temporary), rate, sound)

	print('WebM encoding, frame count, cadence, audio, silent clips and cancellation passed')


if __name__ == '__main__':
	main()
