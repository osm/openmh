#!/usr/bin/env python3
"""Package a configured build or a prepared runtime, with SHA-256 checksums."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

if __package__:
	from .native_archive import check_archive, configured_applications
	from .package_browser_runtime import EXTRACTOR_FILES, RUNTIME_FILES
else:
	from native_archive import check_archive, configured_applications
	from package_browser_runtime import EXTRACTOR_FILES, RUNTIME_FILES


ROOT = Path(__file__).resolve().parents[1]
HASH_CHUNK_BYTES = 1024 * 1024


def file_digest(stream):
	digest = hashlib.sha256()

	for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b''):
		digest.update(chunk)

	return digest.hexdigest()


def publish(source, output):
	destination = output / source.name

	if source.resolve() != destination.resolve():
		shutil.copy2(source, destination)

	with destination.open('rb') as stream:
		digest = file_digest(stream)

	destination.with_name(destination.name + '.sha256').write_text(
		f'{digest}  {destination.name}\n')
	print(f'Release: {destination}', flush=True)


def package_name(build):
	# CPack remains the owner of native artifact names and version numbers.
	config = (build / 'CPackConfig.cmake').read_text()
	match = re.search(r'^set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)',
		config, re.MULTILINE)

	if (not match or
		not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]*', match[1])):
		raise ValueError('Missing or invalid CPack package filename')

	return match[1]


def package_runtime(platform, build, runtime, output):
	if (not (runtime / 'bin').is_dir() or
		not (runtime / 'licenses.txt').is_file()):
		raise ValueError('Prepared runtime requires bin/ and licenses.txt')

	name = package_name(build)

	# Stage only runnable files and notices, beneath one versioned directory.
	with tempfile.TemporaryDirectory(dir=output) as temporary:
		staged = Path(temporary) / name
		shutil.copytree(runtime / 'bin', staged, symlinks=True)
		shutil.copy2(runtime / 'licenses.txt', staged / 'licenses.txt')

		path = Path(shutil.make_archive(str(staged),
			'zip' if platform == 'windows' else 'gztar',
			root_dir=temporary, base_dir=name))

		check_archive(path, configured_applications(build), name)
		publish(path, output)

	sources = runtime / 'dependency-sources.tar.gz'

	if sources.is_file():
		publish(sources, output)


def package_build(build, output, configuration):
	name = package_name(build)

	with tempfile.TemporaryDirectory(dir=output) as temporary:
		subprocess.run(['cpack', '--config', str(build / 'CPackConfig.cmake'),
			'-C', configuration, '-B', temporary], check=True)
		artifacts = [path for path in sorted(Path(temporary).iterdir())
			if path.is_file() and path.name.endswith(('.tar.gz', '.zip'))]

		packages = [path for path in artifacts if path.name != 'dependency-sources.tar.gz']

		if len(packages) != 1:
			raise ValueError('Expected one native release archive from CPack')

		check_archive(packages[0], configured_applications(build), name)

		for artifact in artifacts:
			publish(artifact, output)


def verify_wasm_archive(path, manifest):
	directory = path.name.removesuffix('.tar.gz')
	files = manifest['files']
	required = set(RUNTIME_FILES) | {'licenses.txt'}

	if manifest.get('extractor'):
		required.update(EXTRACTOR_FILES)

	if set(files) != required:
		raise ValueError('Production runtime file list differs from its enabled components')

	expected = {f'{directory}/{name}' for name in ('runtime.json', *files)}

	with tarfile.open(path, 'r:gz') as archive:
		for member in archive:
			if member.name not in expected or not member.isfile():
				raise ValueError(f'Unexpected runtime archive member: {member.name}')

			expected.remove(member.name)
			name = member.name.removeprefix(directory + '/')

			with archive.extractfile(member) as stream:
				if name == 'runtime.json':
					if json.load(stream) != manifest:
						raise ValueError('Archived runtime manifest differs from the build')
				elif not member.size or file_digest(stream) != files[name]:
					raise ValueError(f'Archived runtime checksum mismatch: {name}')

	if expected:
		raise ValueError(f'Missing runtime archive members: {sorted(expected)}')


def package_wasm(build, runtime, output):
	if runtime is None:
		subprocess.run(
			['cmake', '--build', str(build), '--target',
				'wasm-runtime'], check=True)

	source = runtime if runtime is not None else build
	manifest = json.loads((source / 'browser-runtime/runtime.json').read_text())

	if manifest.get('testing') is not False:
		raise ValueError('Cannot release a test runtime')

	if 'licenses.txt' not in manifest.get('files', {}):
		raise ValueError('Set MH_WASM_LICENSE_NOTICES to the combined OpenMH and dependency '
			'notices for this build, or run scripts/build_docker_wasm32')

	version = manifest['version']

	if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]*', version):
		raise ValueError('Invalid browser runtime version')

	archive = source / f'openmh-{version}-wasm.tar.gz'

	with tempfile.TemporaryDirectory(dir=output) as temporary:
		staged = Path(temporary) / archive.name
		shutil.copy2(archive, staged)
		verify_wasm_archive(staged, manifest)
		artifact = staged.replace(output / archive.name)

	publish(artifact, output)
	sources = source / 'dependency-sources.tar.gz'

	if sources.is_file():
		publish(sources, output)


def main():
	parser = argparse.ArgumentParser(
		prog='scripts/release', description=__doc__)
	parser.add_argument('--platform',
		choices=('linux', 'windows', 'wasm'), default='linux')
	parser.add_argument('--build',
		type=Path, help='configured build (default: build/PLATFORM)')
	parser.add_argument('--runtime',
		type=Path, help='prepared Docker runtime directory')
	parser.add_argument('--output',
		type=Path, help='archive directory (default: build/packages/PLATFORM)')
	parser.add_argument('--config',
		default='Release', help='CMake build configuration')

	args = parser.parse_args()

	build = (args.build or ROOT / 'build' / args.platform).resolve()
	output = (
		args.output or ROOT / 'build/packages' / args.platform).resolve()
	runtime = args.runtime.resolve() if args.runtime is not None else None

	try:
		output.mkdir(parents=True, exist_ok=True)

		if args.platform == 'wasm':
			package_wasm(build, runtime, output)
		elif runtime is not None:
			package_runtime(args.platform, build, runtime, output)
		else:
			package_build(build, output, args.config)
	except (ValueError, OSError, KeyError, tarfile.TarError, EOFError, subprocess.CalledProcessError) as error:
		parser.exit(1, f'Release failed: {error}\n')


if __name__ == '__main__':
	main()
