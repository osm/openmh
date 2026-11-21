#!/usr/bin/env python3
"""Export the browser runtime without the website or original game assets."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

SCRIPTS = [
	'openmotor.js', 'database.js', 'package-database.js', 'storage.js',
	'backup.js', 'lifecycle.js', 'checkpoints.js', 'save-session.js',
	'extract-job.js', 'race-history.js', 'multiplayer.js', 'preferences.js', 'player.js', 'session.js',
]
RUNTIME_FILES = SCRIPTS + ['openmotor.wasm', 'package-check.js']
EXTRACTOR_FILES = ['openmotorex.js', 'openmotorex.wasm', 'extract-worker.js',
	'extract-workspace.js']
TEST_FILES = ['mh-browser-single-player-check.js', 'mh-browser-single-player-check.wasm']


def package(build, output, version, extractor=False, testing=False, notices=None):
	files = RUNTIME_FILES.copy()

	if extractor:
		files += EXTRACTOR_FILES

	if testing:
		files += TEST_FILES

	sources = {name: build / name for name in files}

	if notices is not None:
		sources['licenses.txt'] = notices

	# Validate the entire input before changing a previously exported package.
	for source in sources.values():
		if not source.is_file() or not source.stat().st_size:
			raise ValueError(f'Missing or empty runtime file: {source}')

	manifest = {'format': 1, 'version': version,
		'extractor': extractor, 'testing': testing, 'scripts': SCRIPTS,
		'files': {name: hashlib.sha256(source.read_bytes()).hexdigest()
			for name, source in sources.items()}}
	output.mkdir(parents=True, exist_ok=True)
	previous = output / 'runtime.json'

	if previous.exists():
		for name in json.loads(previous.read_text())['files']:
			if Path(name).name != name:
				raise ValueError('Invalid previous runtime filename')

			if name not in sources:
				(output / name).unlink(missing_ok=True)

	for name, source in sources.items():
		destination = output / name

		if not destination.exists() or source.read_bytes() != destination.read_bytes():
			shutil.copy2(source, destination)

	previous.write_text(json.dumps(manifest, indent='\t') + '\n')

	return manifest


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--version', required=True)
	parser.add_argument('--extractor', action='store_true')
	parser.add_argument('--testing', action='store_true')
	parser.add_argument('--notices', type=Path,
		help='combined OpenMH and dependency license notices to include')
	parser.add_argument('--archive', type=Path)
	args = parser.parse_args()

	if args.archive and (args.testing or args.notices is None):
		parser.error('A release archive requires --notices and a production runtime')

	manifest = package(args.build, args.output, args.version, args.extractor, args.testing, args.notices)

	if args.archive:
		directory = args.archive.name.removesuffix('.tar.gz')

		with tarfile.open(args.archive, 'w:gz') as archive:
			for name in ['runtime.json', *manifest['files']]:
				archive.add(args.output / name, arcname=f'{directory}/{name}')

	print(f'Browser runtime {args.version}: {args.output}')


if __name__ == '__main__':
	main()
