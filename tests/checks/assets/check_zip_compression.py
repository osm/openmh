#!/usr/bin/env python3
"""Check compressed resource reads against independently generated ZIP entries."""

import argparse
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zipfile


def check(reader, root, label, data, expected, mode='read', member='Data/test'):
	package = root / (label + '.ass')
	package.write_bytes(data)
	result = subprocess.run([str(reader), mode, str(package), member, str(expected)],
		capture_output=True, text=True, timeout=30)
	assert result.returncode == 0, f'{label}: {result.stdout}{result.stderr}'


def archive(path, payload, level=6):
	with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=level) as output:
		output.writestr('Data/test', payload)
		output.writestr('Empty/', b'')
		output.writestr('Other', b'other' * 1000)
		output.writestr('Stored', b'unchanged', compress_type=zipfile.ZIP_STORED)

	return path.read_bytes()


def headers(data, local_offset, central_offset, value):
	changed = bytearray(data)
	central = struct.unpack_from('<I', data, len(data) - 6)[0]
	struct.pack_into('<I', changed, local_offset, value)
	struct.pack_into('<I', changed, central + central_offset, value)

	return changed


def with_trailing_byte(data, packed):
	"""Insert payload padding while keeping the ZIP index structurally valid."""

	at = 30 + len('Data/test') + packed
	changed = bytearray(data[:at] + b'X' + data[at:])
	central = struct.unpack_from('<I', data, len(data) - 6)[0] + 1
	struct.pack_into('<I', changed, len(changed) - 6, central)
	position = central

	for index in range(struct.unpack_from('<H', data, len(data) - 12)[0]):
		local = struct.unpack_from('<I', changed, position + 42)[0]

		if index:
			struct.pack_into('<I', changed, position + 42, local + 1)

		name, extra, comment = struct.unpack_from('<HHH', changed, position + 28)
		position += 46 + name + extra + comment

	return headers(changed, 18, 20, packed + 1)


def check_valid_entries(reader, root, expected):
	# Large output and large compressed input both cross the buffer boundary.
	for label, payload, level in (
		('repeated', b'motorhead\n' * 20000, 6),
		('random', random.Random(127).randbytes(150000), 6),
		('level0', b'large uncompressed deflate blocks' * 5000, 0),
		('empty', b'', 6)):
		expected.write_bytes(payload)
		data = archive(root / 'source.zip', payload, level)
		check(reader, root, label, data, expected)

	expected.write_bytes(b'unchanged')
	check(reader, root, 'mixed-stored', data, expected, member='Stored')


def check_invalid_entries(reader, root, expected):
	data = archive(root / 'source.zip', b'motorhead\n' * 20000)
	central = struct.unpack_from('<I', data, len(data) - 6)[0]
	packed = struct.unpack_from('<I', data, central + 20)[0]
	size = struct.unpack_from('<I', data, central + 24)[0]

	for label, local, central_field, value, mode in (
		('short-output', 22, 24, size - 1, 'invalid'),
		('long-output', 22, 24, size + 1, 'invalid'),
		('empty-output', 22, 24, 0, 'invalid'),
		('truncated-stream', 18, 20, packed - 1, 'invalid'),
		('wrong-crc', 14, 16, 123, 'invalid'),
		('expanded-limit', 22, 24, 256 * 1024 * 1024 + 1, 'limit'),
		('overlap', 18, 20, packed + 1, 'index-error')):
		check(reader, root, label, headers(data, local, central_field, value), expected, mode)

	broken = bytearray(data)
	broken[30 + len('Data/test')] = 7  # Reserved DEFLATE block type.
	check(reader, root, 'invalid-stream', broken, expected, 'invalid')
	check(reader, root, 'trailing-data', with_trailing_byte(data, packed), expected, 'invalid')

	for label, offset, value in (('unknown-method', 8, 99), ('encryption', 6, 1), ('descriptor', 6, 8)):
		broken = bytearray(data)
		broken[offset] = broken[central + offset + 2] = value
		check(reader, root, label, broken, expected, 'index-error')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--reader', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-zip-read-') as temporary:
		root = Path(temporary)
		expected = root / 'expected'
		check_valid_entries(args.reader, root, expected)
		check_invalid_entries(args.reader, root, expected)

	print('ZIP mixed entries, partial/full reads, limits, truncation, sizes and CRC checks pass')


if __name__ == '__main__':
	main()
