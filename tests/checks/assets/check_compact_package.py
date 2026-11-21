#!/usr/bin/env python3
"""Verify compact package conversion, preservation and unpublished failures."""

import argparse
import hashlib
from pathlib import Path
import select
import shutil
import signal
import struct
import subprocess
import tempfile
import time
import zipfile

from tests.checks.support.build import build_executable


def run(*args):
	result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=180)
	assert result.returncode == 0, result.stdout + result.stderr

	return result


def clips(data):
	assert data[:4] == b'DIVI'
	count = struct.unpack_from('<I', data, 4)[0]
	entries = [struct.unpack_from('<III', data, 12 + i * 12) for i in range(count)]
	offsets = [entry[1] for entry in entries] + [len(data)]

	return entries, [data[a:b] for a, b in zip(offsets, offsets[1:])]


def collection(payloads):
	header = bytearray(780)
	struct.pack_into('<4sII', header, 0, b'DIVI', len(payloads), 780)
	position = 780

	for i, payload in enumerate(payloads):
		struct.pack_into('<III', header, 12 + i * 12, i + 7, position, i + 23)
		position += len(payload)

	return header + b''.join(payloads)


def digest(path):
	with path.open('rb') as stream:
		return hashlib.file_digest(stream, 'sha256').digest()


def make_source(args, root):
	source = root / 'source'
	shutil.copytree(args.gamedir, source, ignore=lambda directory, names:
		[n for n in names if Path(directory) == args.gamedir and
			n.lower() in ('music', 'movies', 'original', 'userdata')])
	music, movies = source / 'Music', source / 'Movies'
	music.mkdir()
	movies.mkdir()
	run(args.ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
		'sine=frequency=440:sample_rate=44100:duration=2', '-ac', '2', music / 'track-02.wav')
	run(args.ffmpeg, '-v', 'error', '-i', music / 'track-02.wav',
		'-c:a', 'libopus', '-b:a', '160k', music / 'track-03.opus')
	(music / 'track-02.opus').write_bytes(b'old alternative: WAV must take precedence')
	webm = root / 'existing.webm'
	run(args.ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
		'testsrc2=size=64x48:rate=25:duration=2', '-c:v', 'libvpx-vp9',
		'-auto-alt-ref', '0', '-lag-in-frames', '0', webm)
	original = next(path for path in (args.gamedir / 'Movies').iterdir()
		if path.suffix.lower() == '.pak')
	_, payloads = clips(original.read_bytes())
	(movies / 'movies.pak').write_bytes(collection([payloads[0], webm.read_bytes()]))

	# Exercise the demo collection names as well as the retail name.
	(movies / 'demo2.pak').write_bytes(collection([webm.read_bytes()]))

	return source


def verify_contents(source, package):
	converted = {'Music/track-02.wav', 'Music/track-02.opus', 'Movies/movies.pak', 'Movies/demo2.pak'}

	with zipfile.ZipFile(package) as archive:
		assert archive.testzip() is None
		assert any(e.compress_type == zipfile.ZIP_DEFLATED for e in archive.infolist())

		for entry in archive.infolist():
			assert entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)

			if entry.filename.split('/')[0].lower() in ('music', 'movies'):
				assert entry.compress_type == zipfile.ZIP_STORED

			if entry.compress_type == zipfile.ZIP_DEFLATED:
				assert entry.compress_size < entry.file_size

			if entry.is_dir() or entry.filename in converted:
				continue

			assert hashlib.sha256(archive.read(entry)).digest() == digest(source / entry.filename), entry.filename

		names = [entry.filename for entry in archive.infolist()]
		assert len(names) == len(set(names)), 'duplicate archive names'
		assert 'Music/track-02.wav' not in names
		assert archive.read('Music/track-02.opus').startswith(b'OggS')

		for name in ('Movies/movies.pak', 'Movies/demo2.pak'):
			old_entries, old_clips = clips((source / name).read_bytes())
			entries, encoded = clips(archive.read(name))
			assert [(e[0], e[2]) for e in entries] == [(e[0], e[2]) for e in old_entries]
			assert all(clip.startswith(b'\x1aE\xdf\xa3') for clip in encoded)

			for before, after in zip(old_clips, encoded):
				if before.startswith(b'\x1aE\xdf\xa3'):
					assert before == after, 'existing WebM was re-encoded'


def check_repack(extractor, source, package, root):
	prepared, repeated = root / 'prepared', root / 'repeated.ass'

	with zipfile.ZipFile(package) as archive:
		archive.extractall(prepared)

	run(extractor, '--compact', prepared, repeated)

	with zipfile.ZipFile(package) as a, zipfile.ZipFile(repeated) as b:
		assert set(a.namelist()) == set(b.namelist())

		for name in a.namelist():
			assert a.read(name) == b.read(name), name

	before = digest(package)
	rejected = subprocess.run([str(extractor), '--compact', str(source), str(package)],
		capture_output=True, timeout=180)
	assert rejected.returncode != 0 and digest(package) == before


def check_cancellation(extractor, source, root):
	destination = root / 'cancelled.ass'
	process = subprocess.Popen([str(extractor), '--compact', str(source), str(destination)],
		stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)

	try:
		deadline = time.monotonic() + 30

		while True:
			ready, _, _ = select.select([process.stderr], [], [], max(0, deadline - time.monotonic()))
			assert ready, 'no conversion progress'
			line = process.stderr.readline()
			assert line, 'extractor ended before cancellation'

			if 'Converting:' in line:
				break

		process.send_signal(signal.SIGTERM)
		_, errors = process.communicate(timeout=30)
		assert process.returncode != 0 and 'interrupted' in errors, errors
	finally:
		if process.poll() is None:
			process.kill()
			process.communicate(timeout=10)

	assert not destination.exists()
	assert not list(root.glob('.mh-*')) and not list(root.glob('openmotorex_tmp_*'))


def check_excluded_conversion(extractor, source, package, root):
	movie = source / 'Movies/movies.pak'
	wave = source / 'Music/track-02.wav'
	original_movie, original_wave = movie.read_bytes(), wave.read_bytes()

	try:
		movie.write_bytes(b'invalid excluded movie')
		music_only = root / 'music-only.ass'
		run(extractor, '--exclude-movies', '--compact', source, music_only)

		with zipfile.ZipFile(music_only) as archive:
			assert not any(name.startswith('Movies/') for name in archive.namelist())
			assert archive.read('Music/track-02.opus').startswith(b'OggS')

		movie.write_bytes(original_movie)
		wave.write_bytes(b'invalid excluded music')
		movies_only = root / 'movies-only.ass'
		run(extractor, '--compact', '--exclude-music', source, movies_only)

		with zipfile.ZipFile(movies_only) as archive, zipfile.ZipFile(package) as complete:
			assert not any(name.startswith('Music/') for name in archive.namelist())

			# The existing compact movie remains byte-identical.
			assert archive.read('Movies/demo2.pak') == complete.read('Movies/demo2.pak')
			_, encoded = clips(archive.read('Movies/movies.pak'))
			assert all(clip.startswith(b'\x1aE\xdf\xa3') for clip in encoded)
	finally:
		movie.write_bytes(original_movie)
		wave.write_bytes(original_wave)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--ffmpeg', default='ffmpeg')
	args = parser.parse_args()
	args.gamedir = args.gamedir.resolve()
	extractor = build_executable(args.build, 'openmotorex').resolve()

	with tempfile.TemporaryDirectory(prefix='mh-compact-package-') as temporary:
		root = Path(temporary)
		source = make_source(args, root)
		before = {p.relative_to(source): digest(p) for p in source.rglob('*') if p.is_file()}
		package = root / 'compact.ass'
		run(extractor, '--compact', source, package)
		verify_contents(source, package)
		check_repack(extractor, source, package, root)
		check_cancellation(extractor, source, root)
		check_excluded_conversion(extractor, source, package, root)
		assert before == {p.relative_to(source): digest(p) for p in source.rglob('*') if p.is_file()}
		(source / 'Movies/movies.pak').write_bytes(collection([b'broken movie']))
		failed = subprocess.run([str(extractor), '--compact', str(source), str(root / 'failed.ass')],
			capture_output=True, timeout=30)
		assert failed.returncode != 0 and not (root / 'failed.ass').exists()
		assert not list(root.glob('.mh-*')) and not list(root.glob('openmotorex_tmp_*'))

	print('Compact media, exclusions, original effects, mixed formats, repacking, cancellation and source preservation passed')


if __name__ == '__main__':
	main()
