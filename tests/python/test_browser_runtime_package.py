"""Ensure runtime exports are complete and independent of website sources."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_browser_runtime',
	ROOT / 'tools/package_browser_runtime.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class RuntimePackageTests(unittest.TestCase):
	def test_archive_contains_one_versioned_directory(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			build, output = root / 'build', root / 'runtime'
			build.mkdir()

			for name in package.RUNTIME_FILES + package.EXTRACTOR_FILES:
				(build / name).write_text(name)

			archive_path = root / 'openmh-0.1.0-rc.1-wasm.tar.gz'
			script = ROOT / 'tools/package_browser_runtime.py'
			notices = root / 'notices.txt'
			notices.write_text('OpenMH and dependency notices')
			command = [sys.executable, '-B', str(script),
				'--build', str(build), '--output', str(output),
				'--version', '0.1.0-rc.1', '--extractor',
				'--archive', str(archive_path)]
			failed = subprocess.run(command, capture_output=True, text=True)
			self.assertNotEqual(failed.returncode, 0)
			self.assertIn('--notices', failed.stderr)
			self.assertFalse(archive_path.exists())

			subprocess.run([*command, '--notices', str(notices)], check=True, capture_output=True)
			manifest = json.loads((output / 'runtime.json').read_text())
			prefix = 'openmh-0.1.0-rc.1-wasm/'

			with tarfile.open(archive_path) as archive:
				self.assertEqual(set(archive.getnames()),
					{prefix + name for name in ['runtime.json', *manifest['files']]})

				for name in ['runtime.json', *manifest['files']]:
					self.assertEqual(archive.extractfile(prefix + name).read(),
						(output / name).read_bytes())

			previous_archive = archive_path.read_bytes()
			previous_files = {path.name: path.read_bytes() for path in output.iterdir()}
			(build / 'database.js').write_text('updated script')

			for name in ('openmotor.wasm', 'openmotorex.wasm'):
				with self.subTest(name=name):
					(build / name).write_bytes(b'')
					result = subprocess.run([*command, '--notices', str(notices)],
						capture_output=True, text=True)
					self.assertNotEqual(result.returncode, 0)
					self.assertIn(name, result.stderr)
					self.assertEqual(archive_path.read_bytes(), previous_archive)
					self.assertEqual({path.name: path.read_bytes()
						for path in output.iterdir()}, previous_files)
					(build / name).write_text(name)

	def test_capabilities_and_stale_files(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			build, output = root / 'build', root / 'runtime'
			build.mkdir()

			for name in package.RUNTIME_FILES + package.EXTRACTOR_FILES + package.TEST_FILES:
				(build / name).write_text(name)

			(build / 'index.html').write_text('must not ship')
			(build / 'game.ass').write_text('must not ship')
			notices = root / 'notices.txt'
			notices.write_text('OpenMH\n' + (ROOT / 'LICENSE.md').read_text() +
				'\nDependency\nsynthetic dependency notices')
			first = package.package(build, output, '0.1.0', extractor=True, testing=True,
				notices=notices)
			self.assertTrue(first['testing'])
			self.assertTrue(first['extractor'])
			self.assertEqual(first['version'], '0.1.0')
			self.assertEqual((output / 'licenses.txt').read_bytes(), notices.read_bytes())
			self.assertNotIn('openmh_license.md', first['files'])
			self.assertNotIn('third_party_licenses.txt', first['files'])
			self.assertNotIn('index.html', first['files'])
			self.assertNotIn('game.ass', first['files'])
			second = package.package(build, output, '0.1.0')
			self.assertNotIn('licenses.txt', second['files'])
			self.assertFalse((output / 'licenses.txt').exists())
			self.assertFalse(second['testing'])
			self.assertFalse(second['extractor'])
			self.assertEqual(json.loads((output / 'runtime.json').read_text()), second)
			self.assertEqual({p.name for p in output.iterdir()}, set(second['files']) | {'runtime.json'})

	def test_missing_input_preserves_export(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			build, output = root / 'build', root / 'runtime'
			build.mkdir()

			for name in package.RUNTIME_FILES:
				(build / name).write_text(name)

			package.package(build, output, '0.1.0')
			original = (output / 'runtime.json').read_bytes()
			(build / 'openmotor.wasm').unlink()

			with self.assertRaises(ValueError):
				package.package(build, output, '0.2.0')

			self.assertEqual(original, (output / 'runtime.json').read_bytes())


if __name__ == '__main__':
	unittest.main()
