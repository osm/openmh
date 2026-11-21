#!/usr/bin/env python3
"""Verify deterministic packaging, standard ZIP compatibility and asset bytes."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from tests.checks.support.build import build_executable


def run_pack(extractor, source, output):
	result = subprocess.run([str(extractor), str(source), str(output)],
		capture_output=True, text=True, timeout=180)
	assert result.returncode == 0, result.stdout + result.stderr


def verify_package(package, source):
	expected_names = set()

	for directory, children, files in os.walk(source, followlinks=True):
		relative = Path(directory).relative_to(source)

		if relative == Path('.'):
			children[:] = [name for name in children if name.lower() not in ('original', 'userdata')]

		expected_names.update((relative / name).as_posix() for name in files)

	actual_names = set()

	with zipfile.ZipFile(package) as archive:
		assert archive.testzip() is None

		for entry in archive.infolist():
			assert entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), entry.filename

			if entry.filename.split('/')[0].lower() in ('music', 'movies'):
				assert entry.compress_type == zipfile.ZIP_STORED, entry.filename

			if entry.compress_type == zipfile.ZIP_DEFLATED:
				assert entry.compress_size < entry.file_size, entry.filename

			assert entry.filename.split('/')[0].lower() not in ('original', 'userdata')

			if entry.is_dir():
				continue

			expected = hashlib.sha256((source / entry.filename).read_bytes()).digest()
			actual = hashlib.sha256(archive.read(entry)).digest()
			assert actual == expected, entry.filename
			actual_names.add(entry.filename)

	assert actual_names == expected_names, (expected_names - actual_names, actual_names - expected_names)
	assert actual_names

	return len(actual_names)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	extractor = build_executable(args.build, 'openmotorex')

	with tempfile.TemporaryDirectory(prefix='mh-package-check-') as temporary:
		root = Path(temporary)
		first, second = root / 'first.ass', root / 'second.ass'
		run_pack(extractor, args.gamedir, first)
		count = verify_package(first, args.gamedir)
		run_pack(extractor, args.gamedir, second)
		original = hashlib.sha256(first.read_bytes()).digest()
		assert hashlib.sha256(second.read_bytes()).digest() == original
		rejected = subprocess.run([str(extractor), str(args.gamedir), str(first)],
			capture_output=True, text=True, timeout=180)
		assert rejected.returncode != 0
		assert hashlib.sha256(first.read_bytes()).digest() == original
		assert sorted(p.name for p in root.iterdir()) == ['first.ass', 'second.ass']
		print(f'{count} files match; ZIP CRCs, deterministic output and exclusive publication pass.')


if __name__ == '__main__':
	main()
