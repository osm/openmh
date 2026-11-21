"""Docker exports replace stale bundles without destroying failed-build outputs."""

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('build_docker', ROOT / 'tools/build_docker.py')
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class DockerBuildTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='openmh docker ')
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name)
		self.output = self.root / 'build/packages'
		self.commands = []
		self.patch_root = patch.object(BUILD, 'ROOT', self.root)
		self.patch_root.start()
		self.addCleanup(self.patch_root.stop)

		for platform in ('linux', 'windows', 'wasm'):
			directory = self.output / platform
			directory.mkdir(parents=True)
			(directory / 'old').write_text(platform)

	def export(self, command, **kwargs):
		self.commands.append(command)
		target = command[command.index('--target') + 1]
		destination = command[command.index('--output') + 1].removeprefix('type=local,dest=')
		platforms = ('linux', 'windows', 'wasm') if target == 'all' else (target,)

		for platform in platforms:
			directory = Path(destination) / platform
			directory.mkdir()
			(directory / 'new').write_text(platform)

	def run_build(self, target, arguments=(), no_cache=False):
		with patch.object(BUILD.subprocess, 'run', side_effect=self.export):
			with redirect_stdout(io.StringIO()):
				BUILD.build(target, arguments, no_cache)

	def test_selected_platform_replaces_only_its_output(self):
		self.run_build('linux_amd64', ['CMAKE_ARGS=-DMH_RENDER_PROFILES=software;openmh'], True)
		self.assertEqual((self.output / 'linux/new').read_text(), 'linux')
		self.assertFalse((self.output / 'linux/old').exists())
		self.assertEqual((self.output / 'windows/old').read_text(), 'windows')
		self.assertEqual((self.output / 'wasm/old').read_text(), 'wasm')
		self.assertIn('CMAKE_ARGS=-DMH_RENDER_PROFILES=software;openmh', self.commands[0])
		self.assertIn('--no-cache', self.commands[0])
		self.assertEqual({p.name for p in self.output.iterdir()}, {'linux', 'windows', 'wasm'})

	def test_all_platforms_replace_stale_files(self):
		self.run_build('all')

		for platform in ('linux', 'windows', 'wasm'):
			self.assertEqual({p.name for p in (self.output / platform).iterdir()}, {'new'})

	def test_cache_options_reach_buildx_without_shell_parsing(self):
		arguments = [
			'--cache-from', 'type=gha,scope=linux_amd64,version=2',
			'--cache-from', 'type=local,src=/cache/previous build',
			'--cache-to', 'type=gha,scope=linux_amd64,version=2,mode=max',
			'--cache-to', 'type=local,dest=/cache/next build',
		]

		with patch.object(BUILD.subprocess, 'run', side_effect=self.export):
			with redirect_stdout(io.StringIO()):
				BUILD.main('linux_amd64', arguments)

		command = self.commands[0]
		start = command.index('--cache-from')
		self.assertEqual(command[start:start + len(arguments)], arguments)
		self.assertEqual((self.output / 'linux/new').read_text(), 'linux')

	def test_failed_or_incomplete_export_preserves_previous_bundles(self):
		for failure in (subprocess.CalledProcessError(1, ['docker']), None):
			with self.subTest(failure=failure):
				with patch.object(BUILD.subprocess, 'run', side_effect=failure):
					with self.assertRaises((ValueError, subprocess.CalledProcessError)):
						BUILD.build('all')

				for platform in ('linux', 'windows', 'wasm'):
					self.assertEqual((self.output / platform / 'old').read_text(), platform)

	def test_failed_replacement_restores_previous_bundle(self):
		with self.assertRaises(FileNotFoundError):
			BUILD.replace_output(self.root / 'missing', self.output / 'linux')

		self.assertEqual((self.output / 'linux/old').read_text(), 'linux')
		self.assertFalse((self.output / '.linux_previous').exists())

	def test_symlink_or_pending_recovery_is_not_overwritten(self):
		outside = self.root / 'outside'
		outside.mkdir()
		link = self.output / 'link'
		link.symlink_to(outside, target_is_directory=True)

		with self.assertRaisesRegex(ValueError, 'regular output directory'):
			BUILD.check_destination(link)

		(self.output / '.linux_previous').mkdir()

		with self.assertRaisesRegex(ValueError, 'recovery'):
			BUILD.build('linux_amd64')


if __name__ == '__main__':
	unittest.main()
