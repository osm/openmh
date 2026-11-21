#!/usr/bin/env python3
"""Exercise the constant guardrail and enforce it on production sources."""

from collections import Counter
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
	'check_constants', ROOT / 'tools/check_constants.py')
CHECK = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CHECK
SPEC.loader.exec_module(CHECK)


class Constants(unittest.TestCase):
	def scan(self, source):
		return CHECK.scan(source, 'src/example.c')

	def test_original_examples_are_rejected(self):
		cases = {
			'file.size = 18 + 768 + (size_t)size * size;': 'byte-size-sum',
			'pixels = malloc(18 + 768 + count);': 'byte-size-sum',
			'pixels = realloc(pixels, 18 + 768 + count);': 'byte-size-sum',
			'pixels = calloc(1, 18 + 768 + count);': 'byte-size-sum',
			r'memcmp(bytes, "PK\003\004", 4);': 'binary-signature',
			r'memcpy(bytes, "PK\x03\x04", 4);': 'binary-signature',
			'n = mh_read_u32le(bytes + 22);': 'binary-offset',
			'mh_rtc_write16(bytes + 0x10UL, n);': 'binary-offset',
			'mh_write_u32le(bytes + offset + 12, n);': 'binary-offset',
		}

		for source, rule in cases.items():
			with self.subTest(source=source):
				self.assertEqual([item.rule for item in self.scan(source)], [rule])

	def test_named_fields_and_ordinary_arithmetic_are_allowed(self):
		self.assertEqual(self.scan(r'''
            #define ZIP_SIGNATURE "PK\003\004"
            enum { HEADER_BYTES = 18, PALETTE_BYTES = 256 * 3 };
            file.size = HEADER_BYTES + sizeof(image.palette) + count;
            pixels = calloc(count, 4);
            name = malloc(length + 1);
            n = mh_read_u32le(bytes + ENTRY_OFFSET + 4 * i);
            n = mh_read_u32le(bytes + i * 4);
            memcpy(bytes, ZIP_SIGNATURE, sizeof(ZIP_SIGNATURE) - 1);
            if (memcmp(bytes, "RIFF", 4) == 0) { i++; }
        '''), [])

	def test_comments_strings_and_nested_arguments(self):
		source = r'''
            /* mh_read_u32le(bytes + 22); */
            // file.size = 18 + 768;
            log("mh_read_u32le(bytes + 22)");
            n = mh_read_u32le(
                (bytes + offset(index, count)) /* field */ + 22U);
        '''
		findings = self.scan(source)
		self.assertEqual(len(findings), 1)
		self.assertEqual(findings[0].rule, 'binary-offset')
		self.assertEqual(findings[0].line, 5)

	def test_baseline_is_exact_and_counts_duplicates(self):
		source = 'n = mh_read_u32le(bytes + 22);'
		finding = self.scan(source)[0]
		baseline = Counter({finding.key: 1})
		self.assertEqual(CHECK.compare(self.scan(source), baseline), [])
		self.assertEqual(len(CHECK.compare(self.scan(source * 2), baseline)), 1)
		self.assertTrue(CHECK.compare(self.scan(source.replace('22', '24')), baseline))
		moved = CHECK.scan(source, 'src/other.c')
		self.assertTrue(CHECK.compare(moved, baseline))
		self.assertIn('stale baseline', CHECK.compare([], baseline)[0])

	def test_asset_directory_literals_use_shared_names(self):
		for literal in ('"/Music"', '"/Game"', '"Sounds/"', '"Tracks/"'):
			with self.subTest(literal=literal):
				findings = self.scan('strcat(path, ' + literal + ');')
				self.assertEqual([item.rule for item in findings],
					['asset-directory'])

		self.assertEqual(self.scan('''
            /* Example: "/Music" */
            #define MH_ASSET_MUSIC_DIR "Music"
            #define MH_ASSET_SOUNDS_PREFIX "Sounds/"
            strcat(path, "/" MH_ASSET_MUSIC_DIR);
            strcpy(path, MH_ASSET_SOUNDS_PREFIX);
            puts("Music");
        '''), [])

	def test_baseline_survives_formatting_but_not_literal_changes(self):
		one = self.scan(r'memcmp(bytes, "PK\003\004", 4);')[0]
		two = self.scan('/* comment */\nmemcmp ( bytes,\n"PK\\003\\004", 4 );')[0]
		self.assertEqual(one.key, two.key)
		other = self.scan(r'memcmp(bytes, "P K\003\004", 4);')[0]
		self.assertNotEqual(one.key, other.key)

	def test_baseline_requires_a_reason(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / 'baseline.tsv'
			path.write_text('src/a.c\tbinary-offset\texpression\t\n')

			with self.assertRaises(ValueError):
				CHECK.read_baseline(path)

	def test_source_scope_excludes_fixtures_and_build_output(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)

			for name in ('src/file.c', 'src/format.h', 'tests/fixture.c',
				'build/generated.c', 'src/script.js'):
				path = root / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text('n = mh_read_u32le(bytes + 22);')

			self.assertEqual([item.path for item in CHECK.source_findings(root)],
				['src/file.c', 'src/format.h'])

	def test_repository_constants(self):
		failures = CHECK.compare(CHECK.source_findings(ROOT),
			CHECK.read_baseline(CHECK.BASELINE))
		self.assertFalse(failures, '\n'.join(failures))


if __name__ == '__main__':
	unittest.main()
