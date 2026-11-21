#!/usr/bin/env python3
"""Compare native TGA decoding to FFmpeg and decode the original FNT corpus.

Local assets only; no proprietary input or generated pixels are checked in.
Requires FFmpeg for an independent TGA decoder, plus a built mh-ui-info.
"""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


from tests.checks.support.process import COMMAND_TIMEOUT


def rgba(path):
	return subprocess.check_output([
		'ffmpeg', '-v', 'error', '-i', str(path), '-f', 'rawvideo',
		'-pix_fmt', 'rgba', '-frames:v', '1', '-threads', '1', '-'], timeout=COMMAND_TIMEOUT)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--info', default='build/mh-ui-info')
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	files = sorted(p for p in (args.gamedir / 'Data').rglob('*') if p.suffix.lower() == '.tga')

	if not files:
		raise SystemExit('no TGA corpus found')

	with tempfile.TemporaryDirectory(prefix='mh-ui-') as temp:
		output = Path(temp) / 'image.rgba'

		for path in files:
			subprocess.check_output([args.info, 'tga', str(path), str(output)], timeout=COMMAND_TIMEOUT)
			native, reference = output.read_bytes(), rgba(path)
			assert native == reference, f'TGA pixel mismatch: {path}'
			subprocess.check_output([args.info, 'sprite', str(path), str(output)], timeout=COMMAND_TIMEOUT)
			keyed = bytearray(reference)

			for i in range(0, len(keyed), 4):
				if keyed[i:i+3] == b'\xff\x00\xff':
					keyed[i+3] = 0

			assert output.read_bytes() == keyed, f'sprite transparency mismatch: {path}'

		fonts = sorted((args.gamedir / 'Data').glob('*.FNT'))
		assert len(fonts) == 4

		for path in fonts:
			dimensions = subprocess.check_output([args.info, 'font', str(path), str(output)], timeout=COMMAND_TIMEOUT)
			width, height = map(int, dimensions.split())
			data = path.read_bytes()
			assert height == struct.unpack_from('<I', data)[0] * 16
			assert width == max(struct.unpack_from('<I', data, 8+i*8)[0] for i in range(256)) * 16
			pixels = output.read_bytes()
			assert len(pixels) == width * height * 4
			assert set(pixels) == {0, 255}
			print(f'{path.name}: {width}x{height}, {sum(pixels[3::4])//255} foreground pixels')

	print(f'{len(files)} TGA files match FFmpeg byte-for-byte; keyed variants and {len(fonts)} FNT files pass')


if __name__ == '__main__':
	main()
