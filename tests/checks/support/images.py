"""Inspect the 640 by 400 BMP captures used by menu checks."""

import struct


def bmp_rgb(path):
	data = path.read_bytes()
	assert data[:2] == b'BM'
	offset = struct.unpack_from('<I', data, 10)[0]
	width, height = struct.unpack_from('<ii', data, 18)
	bits = struct.unpack_from('<H', data, 28)[0]
	compression = struct.unpack_from('<I', data, 30)[0]
	assert width == 640 and abs(height) == 400 and bits in (24, 32)
	assert compression in (0, 3)

	if compression == 3:
		assert struct.unpack_from('<3I', data, 54) == (0xff0000, 0xff00, 0xff)

	stride = ((width * bits + 31) // 32) * 4
	rgb = bytearray()

	for y in range(abs(height)):
		begin = offset + (height - 1 - y if height > 0 else y) * stride
		row = data[begin:begin + stride]

		for x in range(width):
			rgb.extend(row[x * (bits // 8):][:3][::-1])

	return bytes(rgb)
