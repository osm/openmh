#!/usr/bin/env python3
"""Compare WebM playback to FFmpeg without a display or audio device."""

import argparse
import array
import sys
import struct
from pathlib import Path
import subprocess
import tempfile
import zipfile


def run(*arguments):
	return subprocess.run(arguments, check=True, timeout=60, capture_output=True,
		text=True).stdout


def check_clip(ffmpeg, decoder, root, with_audio):
	clip = root / ('sound.webm' if with_audio else 'silent.webm')
	inputs = ['-f', 'lavfi', '-i', 'testsrc2=size=64x48:rate=25:duration=2']

	if with_audio:
		inputs += ['-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=2']

	run(ffmpeg, '-v', 'error', *inputs, '-c:v', 'libvpx-vp9', '-crf', '24',
		'-b:v', '0', '-auto-alt-ref', '0', '-lag-in-frames', '0',
		'-pix_fmt', 'yuv420p', '-colorspace', 'bt709', '-color_range', 'tv',
		'-c:a', 'libopus', '-b:a', '160k', '-ac', '2', str(clip))
	picture_reference = root / 'reference.rgba'
	run(ffmpeg, '-v', 'error', '-y', '-i', str(clip), '-an', '-sws_flags',
		'bilinear', '-pix_fmt', 'rgba', '-f', 'rawvideo', str(picture_reference))
	expected_pcm = b''

	if with_audio:
		audio_reference = root / 'reference.pcm'
		run(ffmpeg, '-v', 'error', '-y', '-i', str(clip), '-vn',
			'-f', 's16le', '-c:a', 'pcm_s16le', str(audio_reference))
		expected_pcm = audio_reference.read_bytes()
		assert len(expected_pcm) == 96000 * 4

	for sound in (0, 1):
		pictures, audio = root / 'actual.rgba', root / 'actual.pcm'
		fields = run(str(decoder), str(clip), str(pictures), str(audio), str(sound)).split()
		assert fields[:3] == ['64', '48', '50'], fields
		assert float(fields[3]) == 40000, fields
		assert fields[4:7] == (['48000', '2', '16'] if with_audio else ['0', '0', '0']), fields
		assert fields[7] == '50', fields
		actual_pixels, expected_pixels = pictures.read_bytes(), picture_reference.read_bytes()
		assert len(actual_pixels) == len(expected_pixels) == 64 * 48 * 4 * 50
		assert all(value == 255 for value in actual_pixels[3::4]), 'alpha differs'

		# libswscale versions/CPU paths differ slightly in chroma rounding.
		differences = [abs(a - b) for a, b in zip(actual_pixels, expected_pixels)]
		assert max(differences) <= 3 and sum(differences) / len(differences) <= 0.6, (
			'RGBA differs', max(differences), sum(differences) / len(differences))
		actual_pcm = audio.read_bytes()
		expected = expected_pcm if sound else b''
		assert len(actual_pcm) == len(expected), 'PCM duration differs'
		a, b = array.array('h', actual_pcm), array.array('h', expected)

		if sys.byteorder != 'little':
			a.byteswap()
			b.byteswap()

		assert all(abs(x - y) <= 1 for x, y in zip(a, b)), 'PCM differs'

	header = bytearray(780)
	struct.pack_into('<4sIIIII', header, 0, b'DIVI', 1, 780, 0, 780, 0)
	package = root / 'movie.ass'

	with zipfile.ZipFile(package, 'w', compression=zipfile.ZIP_STORED) as archive:
		archive.writestr('Movies/', b'')
		archive.writestr('Movies/movies.pak', header + clip.read_bytes())

	package_pixels, package_audio = root / 'package.rgba', root / 'package.pcm'
	package_info = run(str(decoder), str(package), str(package_pixels), str(package_audio), '1')
	assert package_info.split() == fields, 'package metadata differs'
	assert package_pixels.read_bytes() == pictures.read_bytes(), 'package pixels differ'
	assert package_audio.read_bytes() == audio.read_bytes(), 'package audio differs'
	broken = root / 'broken.webm'
	broken.write_bytes(clip.read_bytes()[:20])
	result = subprocess.run([str(decoder), str(broken), str(root / 'bad.rgba'),
		str(root / 'bad.pcm'), '1'], capture_output=True, timeout=10)
	assert result.returncode != 0, 'truncated header was accepted'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--decoder', required=True, type=Path)
	parser.add_argument('--ffmpeg', required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-compact-video-') as temporary:
		root = Path(temporary)
		check_clip(args.ffmpeg, args.decoder.resolve(), root, True)
		check_clip(args.ffmpeg, args.decoder.resolve(), root, False)

		# Shifted audio must be rejected rather than silently moved to time zero.
		shifted = root / 'shifted.webm'
		run(args.ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
			'testsrc2=size=64x48:rate=25:duration=2', '-itsoffset', '0.5',
			'-f', 'lavfi', '-i', 'sine=sample_rate=48000:duration=2',
			'-c:v', 'libvpx-vp9', '-auto-alt-ref', '0', '-lag-in-frames', '0',
			'-c:a', 'libopus', '-ac', '2', str(shifted))
		result = subprocess.run([str(args.decoder.resolve()), str(shifted),
			str(root / 'shifted.rgba'), str(root / 'shifted.pcm'), '1'],
			capture_output=True, timeout=10)
		assert result.returncode != 0, 'shifted audio was accepted without its offset'

	print('WebM pixels, audio, cadence, frame count, silent playback and rejection passed')


if __name__ == '__main__':
	main()
