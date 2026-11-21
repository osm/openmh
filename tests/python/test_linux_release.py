"""Check that local Linux archives resolve their libraries after relocation."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform.startswith('linux') and
	all(shutil.which(name) for name in ('cmake', 'cpack', 'cc', 'objdump')),
	'Linux packaging uses CMake, CPack and a C toolchain')
class LinuxReleaseTests(unittest.TestCase):
	def test_package_matching_libraries_and_notices(self):
		with tempfile.TemporaryDirectory(prefix='openmh linux package ') as temporary:
			root = Path(temporary)
			build = root / 'build'
			runtime = root / 'runtime'
			output = root / 'release'

			(root / 'cmake').mkdir()

			for name in ('release.cmake', 'check_release.cmake.in', 'package_linux.cmake.in'):
				shutil.copy2(ROOT / 'cmake' / name, root / 'cmake' / name)

			(root / 'LICENSE.md').write_text('project')
			(root / 'application.c').write_text('int api(void); int main(void) { return api(); }')
			(root / 'api.c').write_text('int impl(void); int api(void) { return impl(); }')
			(root / 'impl.c').write_text('int impl(void) { return 0; }')
			(root / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.21)\n'
				'project(package_fixture C)\n'
				'set(MH_PACKAGE_VERSION 1.0.0)\n'
				'set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)\n'
				'add_library(impl SHARED impl.c)\n'
				'add_library(SDL3 SHARED api.c)\n'
				'set_target_properties(SDL3 PROPERTIES SOVERSION 0)\n'
				'target_link_libraries(SDL3 PRIVATE impl)\n'
				'add_executable(openmotor application.c)\n'
				'target_link_libraries(openmotor PRIVATE SDL3)\n'
				'include(cmake/release.cmake)\n')
			subprocess.run(['cmake', '-S', str(root), '-B', str(build),
				f'-DMH_LINUX_RUNTIME_DIR={runtime}'], check=True, capture_output=True)
			subprocess.run(['cmake', '--build', str(build)], check=True, capture_output=True)
			command = ['cpack', '--config', str(build / 'CPackConfig.cmake'), '-B', str(output)]
			result = subprocess.run(command, capture_output=True, text=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn('MH_LINUX_RUNTIME_DIR', result.stdout + result.stderr)

			binary = runtime / 'bin'
			binary.mkdir(parents=True)
			notices = 'OpenMH and fixture license notices'
			(runtime / 'licenses.txt').write_text(notices)

			for name in ('libSDL3.so.0', 'libimpl.so'):
				shutil.copy2(build / name, binary / name)

			(binary / 'libunused.so').write_text('unused')
			subprocess.run(command, check=True)

			with tarfile.open(next(output.glob('openmh-*.tar.gz'))) as archive:
				files = {Path(member.name).name for member in archive if member.isfile()}
				self.assertEqual(files, {'openmotor', 'licenses.txt', 'libSDL3.so.0', 'libimpl.so'})
				archive.extractall(root / 'relocated', filter='data')

			moved = next((root / 'relocated').iterdir())
			self.assertEqual((moved / 'licenses.txt').read_text(), notices)
			environ = os.environ.copy()
			environ.pop('LD_LIBRARY_PATH', None)
			subprocess.run([str(moved / 'openmotor')], check=True, env=environ)

			(binary / 'libSDL3.so.0').unlink()
			result = subprocess.run(command, capture_output=True, text=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn('libSDL3.so.0', result.stdout + result.stderr)
