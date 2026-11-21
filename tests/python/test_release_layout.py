"""Check the layout validator against invalid native release archives."""

import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.native_archive import check_archive


class ArchiveLayoutTests(unittest.TestCase):
	def test_reject_extra_entries_wrong_directories_and_missing_notices(self):
		root = 'openmh-1.0.0-linux-x86_64'

		for extra, notices in (
			(root + '/bin/openmotorsv', True),
			('wrong/libz.so.1', True),
			(root + '/game.ass', True),
			(None, False),
		):
			with self.subTest(extra=extra, notices=notices), tempfile.TemporaryDirectory() as tmp:
				path = Path(tmp) / (root + '.tar.gz')
				files = [root + '/openmotorsv']

				if extra:
					files.append(extra)

				if notices:
					files.append(root + '/licenses.txt')

				with tarfile.open(path, 'w:gz') as archive:
					for name in files:
						member = tarfile.TarInfo(name)
						member.size = 1
						archive.addfile(member, io.BytesIO(b'x'))

				with self.assertRaises(ValueError):
					check_archive(path, {'openmotorsv'}, root)

	def test_windows_zip_checks_target_filenames(self):
		with tempfile.TemporaryDirectory() as tmp:
			root = 'openmh-1.0.0-windows-x86_64'
			path = Path(tmp) / (root + '.zip')

			with zipfile.ZipFile(path, 'w') as archive:
				for name in ('openmotorsv.exe', 'libssl.dll', 'licenses.txt'):
					archive.writestr(root + '/' + name, b'fixture')

			build = Path(tmp)
			(build / 'executables-Release.json').write_text(json.dumps(
				{'openmotorsv': str(build / 'openmotorsv.exe')}))
			(build / 'CPackConfig.cmake').write_text(
				f'set(CPACK_PACKAGE_FILE_NAME "{root}")\n')
			subprocess.run([sys.executable, '-B', ROOT / 'tools/check.py',
				'integration', 'release_layout', '--build', build,
				'--archive', path], cwd=build, check=True)

			with self.assertRaisesRegex(ValueError, 'configured native applications'):
				check_archive(path, {'openmotorsv'}, root)

			with self.assertRaisesRegex(ValueError, 'Missing runtime files'):
				check_archive(path, {'openmotorsv.exe', 'openmotorex.exe'}, root)


if __name__ == "__main__":
	unittest.main()
