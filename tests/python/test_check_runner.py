#!/usr/bin/env python3
"""Exercise grouped check discovery, argument forwarding and helper imports."""

import os
from pathlib import Path
import shutil
import tempfile
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tests/check.py"


class CheckRunnerTests(unittest.TestCase):
	def run_check(self, *arguments):
		return subprocess.run([sys.executable, str(RUNNER), *arguments],
			cwd=ROOT, text=True, capture_output=True,
			timeout=20)

	def test_lists_every_check_once(self):
		result = self.run_check("--list")
		self.assertEqual(result.returncode, 0, result.stderr)
		listed = [line.split()[0] for line in result.stdout.splitlines()]

		# Other Python modules provide helpers, not runnable checks.
		expected = [p.stem.removeprefix("check_")
			for p in (ROOT / "tests/checks").glob("*/check_*.py")]
		self.assertEqual(sorted(listed), sorted(expected))
		self.assertEqual(len(listed), len(set(listed)))

	def test_cross_group_imports_and_arguments(self):
		for check in ("shader_contracts", "menu_preview", "pc_secrets",
			"menu_races_live"):
			with self.subTest(check=check):
				result = self.run_check(check, "--help")
				self.assertEqual(result.returncode, 0, result.stderr)
				self.assertIn("--build", result.stdout)

	def test_checks_work_in_an_isolated_tree(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			shutil.copytree(ROOT / "tests/checks", root / "tests/checks",
				ignore=shutil.ignore_patterns("__pycache__"))

			for name in ("__init__.py", "check.py"):
				shutil.copy2(ROOT / "tests" / name, root / "tests" / name)

			code = ("import importlib; from pathlib import Path; "
				"[importlib.import_module('.'.join(p.with_suffix('').parts)) "
				"for p in Path('tests/checks').rglob('*.py')]")
			environment = dict(os.environ)
			environment.pop('PYTHONPATH', None)
			result = subprocess.run([sys.executable, '-c', code], cwd=root,
				env=environment, capture_output=True, text=True, timeout=30)
			self.assertEqual(result.returncode, 0, result.stderr)

			for path in sorted((root / 'tests/checks').glob('*/check_*.py')):
				name = path.stem.removeprefix('check_')
				result = subprocess.run([sys.executable, str(root / 'tests/check.py'),
					name, '--help'], cwd=root, env=environment,
					capture_output=True, text=True, timeout=20)
				self.assertEqual(result.returncode, 0, (name, result.stderr))

	def test_game_inputs_must_be_supplied(self):
		for name, option in (('menu_preview', '--gamedir'),
			('session_settings_live', '--gamedir'),
			('prepared_demo_startup', '--demos'),
			('dropin_installations', '--retail')):
			result = self.run_check(name, '--build', 'build/linux')
			self.assertEqual(result.returncode, 2, (name, result.stderr))
			self.assertIn(option, result.stderr)

	def test_unknown_name_is_an_error(self):
		result = self.run_check("../format")
		self.assertEqual(result.returncode, 2)
		self.assertIn("Unknown check", result.stderr)


if __name__ == "__main__":
	unittest.main()
