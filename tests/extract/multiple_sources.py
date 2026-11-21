"""Exercise the real extraction pipeline using a test-only patch decoder."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile


def run(executable, *arguments, success=True, cwd=None):
	result = subprocess.run([str(executable), *map(str, arguments)],
		capture_output=True, text=True, timeout=30, cwd=cwd)
	assert (result.returncode == 0) == success, result.stdout + result.stderr

	return result


def files(root):
	return {p.relative_to(root).as_posix(): p.read_bytes()
		for p in root.rglob('*') if p.is_file()}


def unpack(path):
	with zipfile.ZipFile(path) as archive:
		assert archive.testzip() is None

		return {entry.filename: archive.read(entry) for entry in archive.infolist()
			if not entry.is_dir()}


def check(executable, production, root):
	source = root / 'base'
	contents = {
		'Game/fixture.trk': b'synthetic track',
		'Game/revision': b'0',
		'Data/keep': b'unchanged data',
		'Music/track02.wav': b'invalid media must be excluded before conversion',
		'Movies/movies.pak': b'invalid media must be excluded before conversion',
		'userdata/save': b'private save',
		'Original/backup': b'original file',
	}

	for name, data in contents.items():
		path = source / name
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)

	patches = {}

	for name, revisions in {'first': b'01', 'second': b'12', 'final': b'02',
		'fail': b'0x', 'cancel': b'0c'}.items():
		patches[name] = root / f'{name}.exe'
		patches[name].write_bytes(b'MH TEST PATCH 1\n' + revisions)

	patch_bytes = {path: path.read_bytes() for path in patches.values()}
	unknown = root / 'unknown.exe'
	unknown.write_bytes(b'unsupported patch')

	# Existing single-source creation and single-patch update remain supported.
	baseline = root / 'baseline.ass'
	run(executable, source, baseline)
	expected = {name: data for name, data in contents.items()
		if name.split('/')[0] not in {'userdata', 'Original'}}
	assert unpack(baseline) == expected
	run(executable, patches['final'], baseline)
	expected['Game/revision'] = b'2'
	assert unpack(baseline) == expected

	# The final argument is the output, and each patch sees preceding changes.
	combined = root / 'combined.ass'
	result = run(executable, source, patches['first'], patches['second'], combined)
	assert unpack(combined) == expected
	assert result.stderr.count('Writing package...') == 1
	progress = re.findall(r'(\d+)% \((\d+)/(\d+) items\)',
		result.stderr.split('Writing package...')[1])

	with zipfile.ZipFile(combined) as archive:
		total = len(archive.infolist())

	assert progress and tuple(map(int, progress[-1])) == (100, total, total)
	assert all(0 <= int(done) <= int(count) == total for _, done, count in progress)
	assert [int(done) for _, done, _ in progress] == sorted(int(done) for _, done, _ in progress)
	assert result.stdout.index(str(patches['first'])) < result.stdout.index(str(patches['second']))

	directory = root / 'directory'
	run(executable, source, patches['final'], directory)
	clean = {name: data for name, data in contents.items()
		if name.split('/')[0] != 'Original'}
	assert files(directory) == {**clean, 'Game/revision': b'2'}

	retained = root / 'retained'
	run(executable, '--keep-original', source, patches['final'], retained)
	assert files(retained) == {**contents, 'Game/revision': b'2',
		'Original/patch.bin': b'2'}

	# Updating a directory applies the same retention policy to old and new files.
	updated = root / 'updated'
	run(executable, '--keep-original', source, updated)
	assert files(updated) == contents
	run(executable, patches['fail'], updated, success=False)
	assert files(updated) == contents
	run(executable, patches['final'], updated)
	assert files(updated) == {**clean, 'Game/revision': b'2'}

	kept_update = root / 'kept_update'
	run(executable, source, kept_update)
	run(executable, '--keep-original', patches['final'], kept_update)
	assert files(kept_update) == {**clean, 'Game/revision': b'2',
		'Original/patch.bin': b'2'}

	# Case-insensitive provenance names must not bypass default cleanup.
	(source / 'Original').rename(source / 'oRiGiNaL')
	mixed = root / 'mixed'
	run(executable, source, mixed)
	assert files(mixed) == clean
	(source / 'oRiGiNaL').rename(source / 'Original')

	for output in (root / 'rejected.ass', combined):
		before = output.read_bytes() if output.exists() else None
		result = run(executable, '--keep-original', source, output, success=False)
		assert '--keep-original requires directory output' in result.stderr
		assert (output.read_bytes() if output.exists() else None) == before

	compact = root / 'compact.ass'
	run(executable, '--compact', '--exclude-music', '--exclude-movies',
		source, patches['final'], compact)
	assert unpack(compact) == {name: data for name, data in expected.items()
		if name.split('/')[0] not in {'Music', 'Movies'}}

	overlay = root / 'overlay.ass'
	run(executable, patches['final'], overlay)
	assert unpack(overlay)['Game/revision'] == b'2'

	# Invalid extra inputs are rejected before any base copying takes place.
	for extra in (unknown, root / 'missing.exe', source):
		output = root / 'invalid.ass'
		result = run(executable, source, extra, output, success=False)
		assert 'Copying files...' not in result.stderr
		assert not output.exists()

	for sources in ((patches['first'], source),
		(source, patches['second'], patches['first']),
		(source, patches['final'], patches['final']),
		(source, patches['fail']), (source, patches['cancel'])):
		for suffix in ('.ass', ''):
			output = root / f'failed{suffix}'
			run(executable, *sources, output, success=False)
			assert not output.exists()
			assert not list(root.glob('openmotorex_tmp_*'))

	# New-output mode must not overwrite either an existing package or directory.
	for output in (combined, directory, patches['final']):
		before = files(output) if output.is_dir() else output.read_bytes()
		run(executable, source, patches['final'], output, success=False)
		assert (files(output) if output.is_dir() else output.read_bytes()) == before

	for arguments in ((), ('--compact',), (source,),
		(source, '--compact', combined),
		(source, patches['final'], '--unknown', combined)):
		run(executable, *arguments, success=False)

	# Terminators are consumed by the parser, while subsequent flag-like paths
	# reach extraction unchanged. Help also works with preceding flags.
	run(executable, '--compact', '--help')
	dash_source = root / '--compact'
	source.rename(dash_source)
	run(executable, '--', '--compact', '--help', cwd=root)
	assert files(root / '--help') == clean
	dash_source.rename(source)
	dash_patch = root / '--patch.exe'
	dash_patch.write_bytes(patches['final'].read_bytes())
	run(executable, 'base', '--', '--patch.exe', 'terminated.ass', cwd=root)
	assert unpack(root / 'terminated.ass')['Game/revision'] == b'2'
	assert dash_patch.read_bytes() == patches['final'].read_bytes()
	run(executable, '--compact=1', '--help', success=False)

	# Synthetic installer bytes must never become accepted by the shipped tool.
	output = root / 'production.ass'
	run(production, source, patches['final'], output, success=False)
	assert not output.exists()
	assert files(source) == contents
	assert all(path.read_bytes() == data for path, data in patch_bytes.items())
	assert not list(root.glob('openmotorex_tmp_*'))


if __name__ == '__main__':
	with tempfile.TemporaryDirectory(prefix='mh-multiple-sources-') as directory:
		check(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(directory))

	print('Multiple sources: ordered patches, one publication, failures, cancellation, '
		'exclusions and source preservation passed without game assets.')
