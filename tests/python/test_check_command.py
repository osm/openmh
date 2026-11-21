"""Check failure reporting and check selection in the developer command."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('check_command', ROOT / 'tools/check.py')
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class CheckCommandTests(unittest.TestCase):
	def commands(self, arguments):
		with patch.object(CHECK, 'run') as run, redirect_stdout(io.StringIO()):
			self.assertEqual(CHECK.main(arguments), 0)

		return [call.args[1] for call in run.call_args_list]

	def test_default_runs_all_checks(self):
		commands = self.commands([])
		self.assertTrue(any(str(ROOT / 'tools/check_constants.py') in cmd
			for cmd in commands))
		self.assertTrue(any('--check' in cmd for cmd in commands))
		self.assertTrue(any('unittest' in cmd for cmd in commands))
		self.assertTrue(any(cmd[0] == 'ctest' for cmd in commands))
		self.assertEqual(commands[-1], [str(ROOT / 'scripts/check_memory')])

	def test_individual_flags_and_combinations(self):
		for flag, script in (('--constants', 'tools/check_constants.py'),
			('--format', 'tools/format.py'), ('--memory', 'scripts/check_memory')):
			commands = self.commands([flag])
			self.assertEqual(len(commands), 1)
			self.assertIn(str(ROOT / script), commands[0])

		commands = self.commands(['--unit'])
		self.assertTrue(any('unittest' in cmd for cmd in commands))
		self.assertTrue(any(cmd[0] == 'ctest' for cmd in commands))
		self.assertFalse(any(str(ROOT / 'scripts/check_memory') in cmd
			for cmd in commands))
		commands = self.commands(['--format', '--constants', '--format'])
		self.assertEqual(len(commands), 2)
		self.assertIn(str(ROOT / 'tools/check_constants.py'), commands[0])
		self.assertIn(str(ROOT / 'tools/format.py'), commands[1])

	def test_failure_stops_later_stages_and_returns_failure(self):
		with patch.object(CHECK.subprocess, 'run',
			side_effect=subprocess.CalledProcessError(7, ['check'])) as run:
			with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
				with self.assertRaises(SystemExit) as failure:
					CHECK.main([])

			self.assertEqual(failure.exception.code, 1)
			self.assertEqual(run.call_count, 1)

	def test_existing_build_applies_requested_configuration(self):
		with tempfile.TemporaryDirectory() as temporary:
			build = Path(temporary)
			(build / 'CMakeCache.txt').touch()

			with patch.object(CHECK, 'run') as run:
				CHECK.unit_checks(build, 'Release', 2)

			configure = next(call.args[1] for call in run.call_args_list
				if '-S' in call.args[1])
			self.assertIn('-DBUILD_TESTING=ON', configure)
			self.assertIn('-DCMAKE_BUILD_TYPE=Release', configure)
			self.assertFalse(any(arg.startswith('-DMH_BUILD_CLIENT=') for arg in configure))

	def test_fresh_build_exercises_default_modules(self):
		with tempfile.TemporaryDirectory() as temporary:
			commands = self.commands(['--unit', '--build', temporary])

		configure = next(command for command in commands if '-S' in command)
		self.assertIn('-DBUILD_TESTING=ON', configure)
		self.assertIn('-DCMAKE_BUILD_TYPE=Debug', configure)
		self.assertNotIn('-DMH_BUILD_CLIENT=OFF', configure)

	def test_integration_preserves_arguments_and_exit_status(self):
		with patch.object(CHECK.subprocess, 'call', return_value=9) as call:
			self.assertEqual(CHECK.main(['integration', 'example',
				'--assets', 'relative path/game.ass']), 9)

		self.assertEqual(call.call_args.args[0][-3:],
			['example', '--assets', 'relative path/game.ass'])
		self.assertNotIn('cwd', call.call_args.kwargs)


if __name__ == '__main__':
	unittest.main()
