"""Application build scripts select platform tools and targets consistently."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BuildCommandTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='openmh build ')
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name)
		self.scripts = self.root / 'scripts'
		self.scripts.mkdir()
		self.commands = self.root / 'commands.jsonl'

		for name in ('build', 'build_openmotor', 'build_openmotorsv', 'build_openmotorex'):
			shutil.copy2(ROOT / 'scripts' / name, self.scripts / name)

		cmake = self.scripts / 'cmake'
		cmake.write_text('#!/usr/bin/env python3\nimport json, os, sys\n'
			'with open(os.environ["BUILD_TEST_LOG"], "a") as log:\n'
			' log.write(json.dumps(sys.argv[1:]) + "\\n")\n')
		cmake.chmod(0o755)
		emcmake = self.scripts / 'emcmake'
		emcmake.write_text('#!/usr/bin/env bash\nexec "$@" -DEMSCRIPTEN=ON\n')
		emcmake.chmod(0o755)
		self.environment = dict(os.environ, BUILD_TEST_LOG=str(self.commands),
			CMAKE_BUILD_PARALLEL_LEVEL='2', PATH=str(self.scripts) + os.pathsep + os.environ['PATH'])

	def run_build(self, script, *arguments):
		self.commands.unlink(missing_ok=True)
		result = subprocess.run([str(self.scripts / script), *arguments],
			cwd=self.root.parent, env=self.environment, capture_output=True, text=True)
		commands = ([json.loads(line) for line in self.commands.read_text().splitlines()]
			if self.commands.exists() else [])

		return result, commands

	def test_client_selects_each_platform(self):
		for platform in ('linux', 'windows', 'wasm'):
			with self.subTest(platform=platform):
				result, commands = self.run_build('build_openmotor', '--platform', platform)
				self.assertEqual(result.returncode, 0, result.stderr)
				configure, compile = commands
				self.assertIn(str(self.root / 'build' / platform), configure)
				self.assertIn('-DMH_BUILD_CLIENT=ON', configure)
				self.assertIn('-DMH_BUILD_EXTRACT=OFF', configure)
				self.assertIn('-DMH_BUILD_SERVER=OFF', configure)
				self.assertIn('wasm-runtime' if platform == 'wasm' else 'openmotor', compile)
				self.assertEqual('-DEMSCRIPTEN=ON' in configure, platform == 'wasm')

	def test_wasm_extractor_does_not_build_client(self):
		result, commands = self.run_build('build_openmotorex', '--platform', 'wasm')
		self.assertEqual(result.returncode, 0, result.stderr)
		self.assertIn('-DMH_BUILD_CLIENT=OFF', commands[0])
		self.assertIn('-DMH_BUILD_EXTRACT=ON', commands[0])
		self.assertEqual(commands[1][-2:], ['--target', 'openmotorex'])

	def test_wasm_server_rejected_before_invoking_tools(self):
		result, commands = self.run_build('build_openmotorsv', '--platform', 'wasm')
		self.assertEqual(result.returncode, 2)
		self.assertIn('dedicated server is not supported', result.stderr)
		self.assertEqual(commands, [])

	def test_full_wasm_bundle_can_omit_extractor(self):
		result, commands = self.run_build('build', '--platform', 'wasm', '-DMH_BUILD_EXTRACT=OFF')
		self.assertEqual(result.returncode, 0, result.stderr)
		definitions = dict(arg[2:].split('=', 1) for arg in commands[0] if arg.startswith('-D'))
		self.assertEqual(definitions['MH_BUILD_EXTRACT'], 'OFF')
		self.assertEqual(definitions['MH_BUILD_SERVER'], 'OFF')
		self.assertEqual(commands[1][-2:], ['--target', 'wasm-runtime'])


if __name__ == '__main__':
	unittest.main()
