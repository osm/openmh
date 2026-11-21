#!/usr/bin/env python3
"""Compare package patching with directory patching, including rejected updates."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import select
import signal
import subprocess
import tempfile
import zipfile

from tests.checks.assets.check_asset_package import verify_package
from tests.checks.support.build import build_executable


def run(extractor, source, destination, *, succeeds=True):
	result = subprocess.run([str(extractor), str(source), str(destination)],
		capture_output=True, text=True, timeout=240)
	assert (result.returncode == 0) == succeeds, result.stdout + result.stderr

	return result


def digest(path):
	with path.open('rb') as stream:
		return hashlib.file_digest(stream, 'sha256').digest()


def assert_clean(root):
	assert not list(root.glob('openmotorex_tmp_*')), list(root.iterdir())
	assert not list(root.glob('.mh-*')), list(root.iterdir())


def reject_unchanged(extractor, patch, package):
	before = digest(package)
	run(extractor, patch, package, succeeds=False)
	assert digest(package) == before, 'failed patch changed the package'
	assert_clean(package.parent)


def corrupt_payload(source, destination):
	shutil.copyfile(source, destination)

	with zipfile.ZipFile(destination) as archive:
		entry = next(info for info in archive.infolist() if not info.is_dir() and info.file_size)

	with destination.open('r+b') as output:
		output.seek(entry.header_offset + 26)
		lengths = output.read(4)
		offset = entry.header_offset + 30 + int.from_bytes(lengths[:2], 'little') + int.from_bytes(lengths[2:], 'little')
		output.seek(offset)
		original = output.read(1)
		output.seek(offset)
		output.write(bytes([original[0] ^ 1]))


def interrupt_patch(extractor, patch, package):
	before = digest(package)

	# The first progress line occurs during unpacking, before patch publication.
	with tempfile.TemporaryFile(mode='w+t') as log:
		process = subprocess.Popen([str(extractor), str(patch), str(package)],
			stdout=log, stderr=subprocess.PIPE, text=True)

		try:
			ready, _, _ = select.select([process.stderr], [], [], 30)
			assert ready, 'no package unpack progress'
			line = process.stderr.readline()
			assert line.strip() == 'Reading package...', line
			process.send_signal(signal.SIGTERM)
			_, errors = process.communicate(timeout=30)
			assert process.returncode != 0 and 'interrupted' in errors, errors
		finally:
			if process.poll() is None:
				process.kill()
				process.communicate(timeout=10)

	assert digest(package) == before, 'interrupted patch changed the package'
	assert_clean(package.parent)


def check_overlay_identity(mount, root, base, overlay, demo=None):
	collection = root / 'overlay-identity'
	collection.mkdir()
	shutil.copyfile(base, collection / 'base.ass')
	candidate = collection / 'patch.ass'

	with zipfile.ZipFile(overlay) as archive:
		marker = archive.read('patch.txt')
		materials = next(name for name in archive.namelist() if name.lower() == 'data/material.mat')
		patch_materials = archive.read(materials)

	with zipfile.ZipFile(base) as archive:
		executable = next(name for name in archive.namelist() if name.lower() == 'motor.exe')
		retail_executable = archive.read(executable)
		retail_materials = archive.read(next(name for name in archive.namelist()
			if name.lower() == materials.lower()))

	cases = [
		('marker only', {}),
		('missing material table', {executable: retail_executable}),
		('unpatched material table', {executable: retail_executable, materials: retail_materials}),
		('modified material table', {materials: bytes([patch_materials[0] ^ 1]) + patch_materials[1:]}),
		('unknown executable', {executable: b'unknown', materials: patch_materials}),
	]

	if demo:
		with zipfile.ZipFile(demo) as archive:
			demo_executable = archive.read(next(name for name in archive.namelist()
				if name.lower() == 'motor.exe'))

		cases.append(('wrong edition', {executable: demo_executable, materials: patch_materials}))

	for label, entries in cases:
		with zipfile.ZipFile(candidate, 'w', zipfile.ZIP_DEFLATED) as archive:
			archive.writestr('patch.txt', marker)

			for name, data in entries.items():
				archive.writestr(name, data)

		result = subprocess.run([str(mount), str(collection)], capture_output=True,
			text=True, timeout=120)
		assert result.returncode != 0, f'{label} accepted: {result.stdout}'

	shutil.copyfile(overlay, candidate)
	result = subprocess.run([str(mount), str(collection)], capture_output=True,
		text=True, timeout=120)
	assert result.returncode == 0, result.stderr

	with zipfile.ZipFile(candidate, 'a', zipfile.ZIP_DEFLATED) as archive:
		archive.writestr(executable, retail_executable)

	result = subprocess.run([str(mount), str(collection)], capture_output=True,
		text=True, timeout=120)
	assert result.returncode == 0, result.stderr


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--source', type=Path, required=True, help='Unpatched European retail CUE/ISO')
	parser.add_argument('--patch', type=Path, required=True)
	parser.add_argument('--demo', type=Path, help='Optional demo installer for wrong-edition rejection')
	args = parser.parse_args()
	extractor = build_executable(args.build, 'openmotorex')

	with tempfile.TemporaryDirectory(prefix='mh-package-patch-') as temporary:
		root = Path(temporary)
		package, directory = root / 'openmotor.ASS', root / 'game'
		run(extractor, args.source, package)
		run(extractor, args.source, directory)
		verify_package(package, directory)
		(root / 'userdata').mkdir()
		save = root / 'userdata' / 'save'
		save.write_bytes(b'keep this save')
		(directory / 'userdata').mkdir(exist_ok=True)
		directory_save = directory / 'userdata' / 'save'
		directory_save.write_bytes(save.read_bytes())

		broken = root / 'corrupt.ass'
		corrupt_payload(package, broken)
		reject_unchanged(extractor, args.patch, broken)

		if os.name == 'posix':
			interrupt_patch(extractor, args.patch, package)

		if args.demo:
			demo = root / 'demo.ass'
			run(extractor, args.demo, demo)
			reject_unchanged(extractor, args.patch, demo)

		layers = root / 'layers'
		layers.mkdir()
		base = layers / 'arbitrary-base.ass'
		shutil.copyfile(package, base)
		original_digest = digest(base)
		overlay = layers / 'aaa-renamed-patch.ass'
		run(extractor, args.patch, overlay)
		assert digest(base) == original_digest, 'creating an overlay changed the base'

		with zipfile.ZipFile(overlay) as archive:
			assert b'kind overlay\n' in archive.read('patch.txt')
			assert len(archive.namelist()) < len(zipfile.ZipFile(base).namelist())

		run(extractor, args.patch, directory)
		run(extractor, args.patch, package)
		assert (directory / 'patch.txt').is_file()
		count = verify_package(package, directory)
		repacked = root / 'repacked.ass'
		run(extractor, directory, repacked)
		assert digest(package) == digest(repacked), 'patch routes produce different packages'
		reject_unchanged(extractor, args.patch, package)
		mount = build_executable(args.build, 'test-package-layers')
		check_overlay_identity(mount, root, base, overlay, demo if args.demo else None)

		def combined(location, reference, revision, succeeds=True):
			result = subprocess.run([str(mount), str(location), str(reference), str(revision)],
				capture_output=True, text=True, timeout=120)
			assert (result.returncode == 0) == succeeds, result.stdout + result.stderr

			return result

		combined(layers, package, 2)
		combined(base, package, 2)
		loose = root / 'loose'

		with zipfile.ZipFile(base) as archive:
			archive.extractall(loose)

		addon = loose / 'bonus.ass'

		with zipfile.ZipFile(addon, 'w') as archive:
			archive.writestr('Game/Bonus.trk', b'test track definition')

		combined(loose, addon, 1)
		shutil.copyfile(overlay, loose / 'patch.ass')
		combined(loose, package, 2)
		combined(loose, addon, 2)
		renamed = root / 'removed-patch.ass'
		overlay.rename(renamed)
		combined(layers, base, 1)
		renamed.rename(overlay)
		another = layers / 'other-base.ass'
		shutil.copyfile(base, another)
		assert 'multiple base' in combined(layers, package, 2, False).stderr
		combined(base, package, 2)
		another.unlink()
		full = root / 'complete'
		full.mkdir()
		shutil.copyfile(package, full / 'all-in-one.ass')
		combined(full, package, 2)
		shutil.copyfile(overlay, full / 'patch.ass')
		assert 'already contains' in combined(full, package, 2, False).stderr
		duplicate = layers / 'duplicate-patch.ass'
		shutil.copyfile(overlay, duplicate)
		assert 'multiple content patches' in combined(layers, package, 2, False).stderr
		duplicate.unlink()

		if args.demo:
			wrong = root / 'wrong-edition'
			wrong.mkdir()
			shutil.copyfile(demo, wrong / 'demo.ass')
			shutil.copyfile(overlay, wrong / 'patch.ass')
			assert 'requires European retail' in combined(wrong, package, 2, False).stderr

		assert digest(base) == original_digest
		assert save.read_bytes() == directory_save.read_bytes() == b'keep this save'
		assert_clean(root)
		print(f'{count} patched assets match merged and layered loading; discovery, unpatching and rejected updates preserve originals and saves.')


if __name__ == '__main__':
	main()
