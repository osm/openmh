#!/usr/bin/env python3
"""Check optional music/movie omission, CLI validation and source preservation."""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from tests.checks.support.build import build_executable


def run(extractor, *arguments, success=True):
	result = subprocess.run([str(extractor), *map(str, arguments)],
		capture_output=True, text=True, timeout=60)
	assert (result.returncode == 0) == success, result.stdout + result.stderr

	return result


def make_source(game, root):
	source = root / 'source'
	source.mkdir()
	executable = next(p for p in game.iterdir() if p.name.lower() == 'motor.exe')
	shutil.copyfile(executable, source / 'motor.exe')

	# Invalid media proves that excluded entries never reach conversion.
	files = {
		'mUsIc/track-02.wav': b'not a WAV',
		'mUsIc/track-03.opus': b'not Opus',
		'mOvIeS/movies.pak': b'not a movie collection',
		'mOvIeS/demo.pak': b'not a demo movie collection',
		'Sounds/effect.wav': b'preserve sound effects',
		'Data/Music/keep.bin': b'only top-level media is excluded',
		'Data/Movies/keep.bin': b'keep nested names',
		'MusicNotes/keep.txt': b'keep directories sharing a prefix',
		'Original/backup.bin': b'omit original backups',
		'userdata/save.bin': b'omit saves',
	}

	for name, data in files.items():
		path = source / name
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)

	return source


def inventory(source):
	return {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).digest()
		for p in source.rglob('*') if p.is_file()}


def verify_package(package, before, excluded):
	omitted = {'original', 'userdata', *excluded}
	expected = {name: digest for name, digest in before.items()
		if name.split('/')[0].lower() not in omitted}

	with zipfile.ZipFile(package) as archive:
		assert archive.testzip() is None
		assert all(name.split('/')[0].lower() not in omitted for name in archive.namelist())
		actual = {entry.filename: hashlib.sha256(archive.read(entry)).digest()
			for entry in archive.infolist() if not entry.is_dir()}

	assert actual == expected, (actual.keys() ^ expected.keys())


def check_selections(extractor, source, root, before):
	cases = (
		((), set()),
		(('--exclude-music',), {'music'}),
		(('--exclude-movies',), {'movies'}),
		(('--exclude-movies', '--exclude-music'), {'music', 'movies'}),
		(('--compact', '--exclude-music', '--exclude-movies'), {'music', 'movies'}),
		(('--exclude-movies', '--compact', '--exclude-music'), {'music', 'movies'}),
	)

	for index, (options, excluded) in enumerate(cases):
		package = root / f'selection-{index}.ass'
		run(extractor, *options, source, package)
		verify_package(package, before, excluded)
		digest = hashlib.sha256(package.read_bytes()).digest()
		run(extractor, *options, source, package, success=False)
		assert hashlib.sha256(package.read_bytes()).digest() == digest

	assert (root / 'selection-3.ass').stat().st_size < (root / 'selection-0.ass').stat().st_size


def check_rejections(extractor, source, root):
	for flag in ('--exclude-music', '--exclude-movies'):
		directory = root / flag[2:]
		rejected = run(extractor, flag, source, directory, success=False)
		assert '.ass package destination' in rejected.stderr
		assert not directory.exists()

	output = root / 'invalid.ass'

	for arguments in (('--unknown', source, output), ('--exclude-music',),
		('--exclude-movies', source),
		('--exclude-music', source, output, 'extra')):
		run(extractor, *arguments, success=False)
		assert not output.exists()


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	extractor = build_executable(args.build, 'openmotorex')

	with tempfile.TemporaryDirectory(prefix='mh-package-exclusions-') as temporary:
		root = Path(temporary)
		source = make_source(args.gamedir, root)
		before = inventory(source)
		check_selections(extractor, source, root, before)
		check_rejections(extractor, source, root)
		assert inventory(source) == before, 'source files changed'
		assert not list(root.glob('.mh-*')) and not list(root.glob('openmotorex_tmp_*'))

	print('Independent exclusions, combined compact flags, exact retained bytes, '
		'CLI rejection and source preservation passed.')


if __name__ == '__main__':
	main()
