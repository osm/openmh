"""A parity check must reject incomplete, corrupt and divergent evidence."""

import csv
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tests.checks.browser.check_simulation_parity import compare_traces


class SimulationParityTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory()
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name)
		self.keys = [["driving", "1", "120", "1"],
			["driving", "1", "120", "2"]]
		self.rows = [key + ["0"] * 512 + ["0/3/1;"] for key in self.keys]

	def compare(self, left, right):
		paths = [self.root / "native.csv", self.root / "wasm.csv"]

		for path, rows in zip(paths, (left, right)):
			with path.open("w", newline="") as stream:
				csv.writer(stream).writerows(rows)

		with patch("tests.checks.browser.check_simulation_parity.expected_ticks",
			return_value=iter(self.keys)):
			return compare_traces(*paths)

	def test_numeric_equality_ignores_printf_spelling(self):
		other = [row.copy() for row in self.rows]
		other[0][4] = "0.000e+00"
		self.assertEqual(self.compare(self.rows, other), 2)

	def test_changed_physics_identifies_tick_and_field(self):
		other = [row.copy() for row in self.rows]
		other[1][6] = "0.0000000000000001"

		with self.assertRaisesRegex(ValueError, r"rate=120 tick=2 value\[2\]"):
			self.compare(self.rows, other)

	def test_both_empty_or_truncated_is_not_success(self):
		for rows in ([], self.rows[:1], self.rows + self.rows[:1]):
			with self.subTest(length=len(rows)):
				with self.assertRaisesRegex(ValueError, "trace length"):
					self.compare(rows, rows)

	def test_missing_or_reordered_tick_is_not_success(self):
		with self.assertRaisesRegex(ValueError, "malformed or missing"):
			self.compare(self.rows, list(reversed(self.rows)))

	def test_nonfinite_values_cannot_match(self):
		for value in ("nan", "inf", "-inf"):
			with self.subTest(value=value):
				other = [row.copy() for row in self.rows]
				other[0][4] = value

				with self.assertRaisesRegex(ValueError, r"value\[0\]"):
					self.compare(other, other)

	def test_checkpoint_change_is_not_success(self):
		other = [row.copy() for row in self.rows]
		other[0][-1] = "0/4/1;"

		with self.assertRaisesRegex(ValueError, "visited checkpoints differ"):
			self.compare(self.rows, other)


if __name__ == "__main__":
	unittest.main()
