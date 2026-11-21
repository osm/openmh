"""Validate public build selections without platform SDK discovery."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class BuildProfilesTests(unittest.TestCase):
	def configure(self, *options):
		with tempfile.TemporaryDirectory(prefix="mh-profile-options-") as directory:
			root = Path(directory)
			(root / "CMakeLists.txt").write_text(
				"cmake_minimum_required(VERSION 3.16)\n"
				"project(ProfileOptions NONE)\n"
				"set(MH_BUILD_CLIENT ON)\n"
				f'include("{ROOT / "cmake/profiles.cmake"}")\n',
				encoding="utf-8",
			)

			return subprocess.run(
				["cmake", "-S", str(root), "-B", str(root / "build"), *options],
				capture_output=True,
				text=True,
				check=False,
			)

	def test_sparse_and_empty_optional_families(self):
		result = self.configure(
			"-DMH_RENDER_PROFILES=powervr;software;powervr",
			"-DMH_AUDIO_PROFILES=a3d12;eax",
			"-DMH_INPUT_DEVICES=keyboard",
			"-DMH_SCREENSHOT_FORMATS=",
			"-DMH_IMPORT_FORMATS=",
		)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_openmh_profile(self):
		for profiles in ("openmh", "software;glide;direct3d;powervr;openmh"):
			with self.subTest(profiles=profiles):
				result = self.configure(f"-DMH_RENDER_PROFILES={profiles}")
				self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_invalid_selections_fail_early(self):
		for option in (
			"-DMH_RENDER_PROFILES=",
			"-DMH_AUDIO_PROFILES=",
			"-DMH_RENDER_PROFILES=openmh;typo",
			"-DMH_AUDIO_PROFILES=software;typo",
			"-DMH_INPUT_DEVICES=joystick",
			"-DMH_INPUT_DEVICES=keyboard;typo",
			"-DMH_SCREENSHOT_FORMATS=jpeg",
			"-DMH_IMPORT_FORMATS=typo",
		):
			with self.subTest(option=option):
				result = self.configure(option)
				self.assertNotEqual(result.returncode, 0, result.stdout)
				self.assertIn(option.split("=")[0][2:], result.stderr)


if __name__ == "__main__":
	unittest.main()
