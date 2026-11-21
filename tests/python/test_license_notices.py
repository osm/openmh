"""Release notices retain the project and platform dependency licenses."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('export_sources', ROOT / 'docker/export_sources.py')
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


class LicenseNoticeTests(unittest.TestCase):
	def test_combined_notices_for_each_platform(self):
		project = (ROOT / 'LICENSE.md').read_text()
		read_text = Path.read_text

		for platform in ('linux', 'windows', 'wasm'):
			with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
				output = Path(temporary)
				expected = []

				def read_notice(path, *args, **kwargs):
					if str(path).startswith(('/deps/', '/usr/share/', '/sdk/')):
						text = f'Copyright and license for {path}\n'
						expected.append(text)

						if path.name == 'md5c.c':
							return text + '#include <unused.h>\nimplementation'

						return text

					return read_text(path, *args, **kwargs)

				with mock.patch.object(Path, 'read_text', read_notice), mock.patch.object(
					EXPORT, 'wasm_notices', return_value={'emscripten.txt': '/sdk/LICENSE'}):
					EXPORT.export_notices(output, platform)

				combined = (output / 'licenses.txt').read_text()
				self.assertEqual(combined.count(project), 1)

				for name in ('sdl.txt', 'zlib.txt', 'unshield.txt', 'unshield-md5.txt',
					'ffmpeg.txt', 'ffmpeg-notes.txt', 'opus.txt', 'vpx.txt', 'vpx-patents.txt'):
					self.assertIn(name + '\n' + '=' * len(name), combined)

				for text in expected:
					self.assertIn(text, combined)

				self.assertNotIn('implementation', combined)
				self.assertEqual('openssl.txt' in combined, platform != 'wasm')
				self.assertEqual('mingw-w64.txt' in combined, platform == 'windows')
				self.assertEqual('emscripten.txt' in combined, platform == 'wasm')
				self.assertEqual({path.name for path in output.iterdir()},
					{'licenses.txt', 'THIRD_PARTY.txt'})


if __name__ == '__main__':
	unittest.main()
