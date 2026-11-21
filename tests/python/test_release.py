#!/usr/bin/env python3
"""Release bundles contain runtime files, with sources and checksums separate."""

import hashlib
import sys
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools import release as RELEASE


class ReleaseTests(unittest.TestCase):
	def configure_windows_package(self, root, runtime):
		# Exercise Windows packaging without requiring a Windows compiler.
		(root / 'cmake').mkdir()

		for name in ('release.cmake', 'windows_runtime.cmake',
			'check_release.cmake.in', 'package_sources.cmake.in'):
			shutil.copy2(ROOT / 'cmake' / name, root / 'cmake' / name)

		(root / 'LICENSE.md').write_text('project license')
		(root / 'openmotorsv.exe').write_bytes(b'current executable')
		(root / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.21)\n'
			'project(package_fixture NONE)\n'
			'set(WIN32 TRUE)\n'
			'set(CMAKE_SYSTEM_NAME Windows)\n'
			'set(CMAKE_SYSTEM_PROCESSOR x86_64)\n'
			'set(MH_PACKAGE_VERSION 1.0.0)\n'
			'install(PROGRAMS openmotorsv.exe DESTINATION . COMPONENT Runtime)\n'
			'include(cmake/release.cmake)\n')
		build = root / 'build'
		subprocess.run(['cmake', '-S', str(root), '-B', str(build),
			f'-DMH_WINDOWS_RUNTIME_DIR={runtime}'],
			check=True, capture_output=True, text=True)
		(build / 'executables-Release.json').write_text(json.dumps(
			{'openmotorsv': str(root / 'openmotorsv.exe')}))

		return build

	def check_digest(self, path):
		expected = hashlib.sha256(path.read_bytes()).hexdigest()
		self.assertEqual(path.with_name(path.name + '.sha256').read_text(),
			f'{expected}  {path.name}\n')

	@unittest.skipUnless(shutil.which('cmake') and shutil.which('cpack'),
		'CMake and CPack are required')
	def test_local_windows_package_uses_prepared_runtime(self):
		with tempfile.TemporaryDirectory(prefix='openmh package ') as temporary:
			root = Path(temporary)
			runtime = root / 'runtime'
			(runtime / 'bin').mkdir(parents=True)
			libraries = {
				'libssl-3-x64.dll': b'SSL library',
				'libcrypto-3-x64.dll': b'crypto library',
				'codec.DLL': b'codec library',
				'libwinpthread-1.dll': b'compiler runtime',
			}

			for name, data in libraries.items():
				(runtime / 'bin' / name).write_bytes(data)

			(runtime / 'bin/openmotorsv.exe').write_bytes(b'old executable')
			(runtime / 'bin/unrelated.ass').write_bytes(b'game assets')
			licenses = b'OpenMH\nproject license\nOpenSSL\nlibrary license\n'
			(runtime / 'licenses.txt').write_bytes(licenses)
			(runtime / 'dependency-sources.tar.gz').write_bytes(b'sources')
			build = self.configure_windows_package(root, runtime)
			output = root / 'release'
			output.mkdir()

			RELEASE.package_build(build, output, 'Release')

			name = 'openmh-1.0.0-windows-x86_64'
			artifact = output / (name + '.zip')
			expected = dict(libraries, **{
				'openmotorsv.exe': b'current executable',
				'licenses.txt': licenses,
			})

			with zipfile.ZipFile(artifact) as archive:
				contents = {entry.filename: archive.read(entry)
					for entry in archive.infolist() if not entry.is_dir()}

			self.assertEqual(contents,
				{name + '/' + filename: data for filename, data in expected.items()})
			self.check_digest(artifact)
			self.assertEqual((output / 'dependency-sources.tar.gz').read_bytes(),
				b'sources')
			self.check_digest(output / 'dependency-sources.tar.gz')

			previous = artifact.read_bytes()
			(root / 'openmotorsv.exe').write_bytes(b'')

			with self.assertRaisesRegex(ValueError, 'openmotorsv.exe'):
				RELEASE.package_build(build, output, 'Release')

			self.assertEqual(artifact.read_bytes(), previous)
			self.check_digest(artifact)

	@unittest.skipUnless(shutil.which('cmake') and shutil.which('cpack'),
		'CMake and CPack are required')
	def test_local_windows_package_requires_runtime_and_notices(self):
		for has_bin in (False, True):
			with self.subTest(has_bin=has_bin), tempfile.TemporaryDirectory() as temporary:
				root = Path(temporary)
				runtime = root / 'runtime'

				if has_bin:
					(runtime / 'bin').mkdir(parents=True)

				build = self.configure_windows_package(root, runtime)
				result = subprocess.run(['cpack', '--config',
					str(build / 'CPackConfig.cmake'), '-B', str(root / 'release')],
					capture_output=True, text=True)

				self.assertNotEqual(result.returncode, 0)
				self.assertIn('licenses.txt', result.stdout + result.stderr)

	def test_prepared_runtime_excludes_sources_and_previous_packages(self):
		for platform, extension in (('linux', '.tar.gz'), ('windows', '.zip')):
			with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
				root = Path(temporary)
				(root / 'CPackConfig.cmake').write_text(
					'set(CPACK_PACKAGE_FILE_NAME "openmh-test")\n')
				runtime = root / 'runtime'
				(runtime / 'bin').mkdir(parents=True)
				program = 'openmotor.exe' if platform == 'windows' else 'openmotor'
				library = 'example.dll' if platform == 'windows' else 'libexample.so'
				(root / 'executables-Release.json').write_text(json.dumps(
					{'openmotor': str(root / program)}))
				(runtime / 'bin' / program).write_bytes(b'synthetic executable')
				(runtime / 'bin' / program).chmod(0o755)
				(runtime / 'bin' / library).write_bytes(b'synthetic library')
				(runtime / 'licenses.txt').write_text(
					'OpenMH\nsynthetic project license\nDependency\nsynthetic license')
				(runtime / 'THIRD_PARTY.txt').write_text('synthetic dependencies')
				(runtime / 'runtime-libraries.txt').write_text('build diagnostics')
				(runtime / 'dependency-sources.tar.gz').write_bytes(b'sources')
				(runtime / 'unrelated.ass').write_bytes(b'not a runtime file')

				for attempt in range(2):
					RELEASE.package_runtime(platform, root, runtime, runtime)
					artifact = runtime / ('openmh-test' + extension)

					if platform == 'windows':
						with zipfile.ZipFile(artifact) as archive:
							names = {entry.filename for entry in archive.infolist()
								if not entry.is_dir()}
							mode = archive.getinfo('openmh-test/' + program).external_attr >> 16
							licenses = archive.read('openmh-test/licenses.txt')
					else:
						with tarfile.open(artifact) as archive:
							names = {member.name for member in archive if member.isfile()}
							mode = archive.getmember('openmh-test/' + program).mode
							licenses = archive.extractfile('openmh-test/licenses.txt').read()

					self.assertEqual(names, {'openmh-test/' + program,
						'openmh-test/' + library, 'openmh-test/licenses.txt'})
					self.assertEqual(licenses, (runtime / 'licenses.txt').read_bytes())
					self.assertEqual(mode & 0o777, 0o755)
					self.check_digest(artifact)
					self.check_digest(runtime / 'dependency-sources.tar.gz')

	def test_incomplete_native_runtime_preserves_release(self):
		for platform, suffix in (('linux', ''), ('windows', '.exe')):
			with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
				root = Path(temporary)
				binary = root / 'bin'
				binary.mkdir()
				(root / 'CPackConfig.cmake').write_text(
					'set(CPACK_PACKAGE_FILE_NAME "openmh-test")\n')
				targets = {name: str(root / (name + suffix))
					for name in ('openmotor', 'openmotorsv')}
				(root / 'executables-Release.json').write_text(json.dumps(targets))
				(root / 'licenses.txt').write_text('notices')
				archive = root / ('openmh-test.zip' if suffix else 'openmh-test.tar.gz')
				archive.write_bytes(b'previous release')
				checksum = archive.with_name(archive.name + '.sha256')
				checksum.write_text('previous checksum')

				for state in ('empty directory', 'missing server', 'empty server'):
					with self.subTest(state=state):
						if state == 'missing server':
							(binary / ('openmotor' + suffix)).write_bytes(b'client')
						elif state == 'empty server':
							(binary / ('openmotorsv' + suffix)).touch()

						with self.assertRaises(ValueError):
							RELEASE.package_runtime(platform, root, root, root)

						self.assertEqual(archive.read_bytes(), b'previous release')
						self.assertEqual(checksum.read_text(), 'previous checksum')

	def test_wasm_release_name_and_checksum(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			runtime = root / 'browser-runtime'
			runtime.mkdir()
			(runtime / 'licenses.txt').write_bytes(b'notices')

			for name in RELEASE.RUNTIME_FILES:
				(runtime / name).write_bytes(name.encode())

			files = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
				for path in runtime.iterdir()}
			(runtime / 'runtime.json').write_text(json.dumps(
				{'version': '1.0', 'testing': False, 'files': files}))
			archive = root / 'openmh-1.0-wasm.tar.gz'

			def write_archive():
				with tarfile.open(archive, 'w:gz') as bundle:
					for path in runtime.iterdir():
						bundle.add(path, 'openmh-1.0-wasm/' + path.name)

			write_archive()
			output = root / 'release'
			output.mkdir()

			RELEASE.package_wasm(root, root, output)

			self.assertEqual((output / archive.name).read_bytes(), archive.read_bytes())
			self.check_digest(output / archive.name)

			previous = (output / archive.name).read_bytes()
			(runtime / 'openmotor.wasm').write_bytes(b'older engine')
			write_archive()

			with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
				RELEASE.package_wasm(root, root, output)

			(runtime / 'openmotor.wasm').unlink()
			write_archive()

			with self.assertRaisesRegex(ValueError, 'Missing runtime archive members'):
				RELEASE.package_wasm(root, root, output)

			self.assertEqual((output / archive.name).read_bytes(), previous)
			self.check_digest(output / archive.name)

	def test_reject_test_runtime_and_unsafe_package_name(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			(root / 'browser-runtime').mkdir()
			(root / 'browser-runtime/runtime.json').write_text(json.dumps(
				{'version': '1.0', 'testing': True}))

			with self.assertRaisesRegex(ValueError, 'test runtime'):
				RELEASE.package_wasm(root, root, root)

			(root / 'browser-runtime/runtime.json').write_text(json.dumps(
				{'version': '1.0', 'testing': False, 'files': {}}))

			with self.assertRaisesRegex(ValueError, 'MH_WASM_LICENSE_NOTICES'):
				RELEASE.package_wasm(root, root, root)

			(root / 'CPackConfig.cmake').write_text(
				'set(CPACK_PACKAGE_FILE_NAME "../outside")\n')

			with self.assertRaisesRegex(ValueError, 'package filename'):
				RELEASE.package_name(root)


if __name__ == '__main__':
	unittest.main()
