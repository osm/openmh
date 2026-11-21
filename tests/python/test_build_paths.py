#!/usr/bin/env python3
"""Resolve configured executables without falling back to stale build outputs."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.checks.support.build import build_executable


class BuildPathsTests(unittest.TestCase):
	def test_separate_outputs_and_disabled_target(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			entries = {name: str(root / suffix) for name, suffix in (
				("openmotor", "openmotor"), ("mh-model-viewer", "tools/mh-model-viewer"),
				("test-cheats", "tests/test-cheats"))}
			(root / "executables-Release.json").write_text(json.dumps(entries))
			(root / "mh-disabled").touch()

			with patch.dict("os.environ", {"MH_BUILD_CONFIG": ""}):
				for name, path in entries.items():
					self.assertEqual(build_executable(root, name), Path(path))

				with self.assertRaisesRegex(ValueError, "not configured"):
					build_executable(root, "mh-disabled")

	def test_explicit_configuration(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)

			for config in ("Debug", "Release"):
				(root / f"executables-{config}.json").write_text(
					json.dumps({"openmotor": str(root / config / "openmotor")}))

			with patch.dict("os.environ", {"MH_BUILD_CONFIG": ""}):
				with self.assertRaisesRegex(ValueError, "MH_BUILD_CONFIG"):
					build_executable(root, "openmotor")

			with patch.dict("os.environ", {"MH_BUILD_CONFIG": "Release"}):
				self.assertEqual(build_executable(root, "openmotor"), root / "Release/openmotor")

	def test_unconfigured_directory(self):
		with tempfile.TemporaryDirectory() as directory:
			with patch.dict("os.environ", {"MH_BUILD_CONFIG": ""}):
				with self.assertRaisesRegex(ValueError, "Configure"):
					build_executable(directory, "openmotor")


if __name__ == "__main__":
	unittest.main()
