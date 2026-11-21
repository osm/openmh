"""Shared live-check ownership and private installation fixtures."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.checks.live import check_dropin_installations as dropin
from tests.checks.live.check_menu_ghost_preview import fixture as recording_fixture
from tests.checks.support.installation import prepare_view
from tests.checks.support.process import stop
from tests.checks.support.x11 import key


class ToolSupportTests(unittest.TestCase):
	def test_installation_does_not_share_user_data(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root/'source'
			source.mkdir()

			for name in ('Game', 'userdata', 'Original'):
				(source/name).mkdir()

			(source/'userdata/settings.cfg').write_text('original settings')
			(source/'motor.exe').write_bytes(b'fixture')
			destination = root/'private'
			prepare_view(destination, source)
			self.assertEqual((destination/'Game').resolve(), source/'Game')
			self.assertEqual((destination/'motor.exe').read_bytes(), b'fixture')
			self.assertFalse((destination/'userdata').exists())
			self.assertFalse((destination/'Original').exists())
			(destination/'userdata').mkdir()
			(destination/'userdata/settings.cfg').write_text('test settings')
			self.assertEqual((source/'userdata/settings.cfg').read_text(), 'original settings')

			with self.assertRaises(FileExistsError):
				prepare_view(destination, source)

	def test_recording_fixture_keeps_existing_saves_private(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			source = root / 'source'

			for name in ('Game', 'Demos', 'userdata', 'Original'):
				(source / name).mkdir(parents=True)

			(source / 'userdata/motorhead.cfg').write_text('original')
			(source / 'Game/Keyboard.clo').write_text('profile')
			game = root / 'private'
			demos = recording_fixture(game, source, ['Test'])
			self.assertTrue((demos / 'Test.MDE').is_file())
			self.assertFalse((game / 'userdata').exists())
			self.assertFalse((game / 'Original').exists())
			(game / 'userdata').mkdir()
			(game / 'userdata/motorhead.cfg').write_text('changed')
			self.assertEqual((source / 'userdata/motorhead.cfg').read_text(), 'original')
			self.assertFalse((source / 'Demos/Test.MDE').exists())

	def test_dropin_uses_plain_installations_without_metadata(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			retail = root / 'retail'
			demos = root / 'demos'
			demo = demos / 'demo'
			components = root / 'patch'
			reports = root / 'reports'

			for path in (retail, demo, components / 'Data',
				components / 'Program Executable Files'):
				path.mkdir(parents=True)

			(retail / 'motor.exe').write_bytes(b'retail')
			(demo / 'motor.exe').write_bytes(b'demo')
			(components / 'Program Executable Files/motor.exe').write_bytes(b'30')
			(components / 'Data/asset').write_bytes(b'updated')
			patch31 = root / 'patch31.zip'

			with zipfile.ZipFile(patch31, 'w') as archive:
				archive.writestr('motor.exe', b'31')

			expected = {'retail-12': b'retail', 'retail-30': b'30',
				'retail-31': b'31', 'demo': b'demo'}

			def startup(binary, installation, cwd, output):
				self.assertEqual((installation / 'motor.exe').read_bytes(),
					expected[installation.name])

				if installation.name in ('retail-30', 'retail-31'):
					self.assertEqual((installation / 'asset').read_bytes(), b'updated')

				return {'edition': installation.name, 'passed': True}

			arguments = ['check', '--build', str(root), '--retail', str(retail),
				'--demos', str(demos), '--patch-components', str(components),
				'--patch31', str(patch31), '--reports', str(reports)]

			with patch.object(sys, 'argv', arguments), \
				patch.object(dropin, 'build_executable', return_value=root / 'openmotor'), \
				patch.object(dropin, 'check_startup', side_effect=startup), \
				redirect_stdout(io.StringIO()):
				self.assertEqual(dropin.main(), 0)

			results = json.loads((reports / 'startup.json').read_text())
			self.assertEqual({entry['edition'] for entry in results}, set(expected))
			self.assertEqual((retail / 'motor.exe').read_bytes(), b'retail')
			self.assertFalse((retail / 'asset').exists())
			self.assertEqual((demo / 'motor.exe').read_bytes(), b'demo')

	def test_failed_frame_wait_releases_key(self):
		with patch('tests.checks.support.x11.xdo') as xdo, \
			patch('tests.checks.support.x11.frames', return_value=0), \
			patch('tests.checks.support.x11.wait_frames', side_effect=RuntimeError('exited')):
			with self.assertRaisesRegex(RuntimeError, 'exited'):
				key(1, 'Return', None, None)

			self.assertEqual(xdo.call_args.args, ('keyup', 'Return'))

	def test_unresponsive_child_is_killed_and_reaped(self):
		process = Mock()
		process.poll.return_value = None
		process.wait.side_effect = [subprocess.TimeoutExpired('fixture', 5), -9]
		stop(process)
		process.terminate.assert_called_once()
		process.kill.assert_called_once()
		self.assertEqual(process.wait.call_count, 2)
		process.reset_mock()
		process.poll.return_value = 0
		stop(process)
		process.terminate.assert_not_called()


if __name__ == '__main__':
	unittest.main()
