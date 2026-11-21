#!/usr/bin/env python3
"""Dependency preparation rejects unverified sources before extraction or build."""

import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
	'install_dependency', ROOT / 'docker/install_dependency.py')
INSTALL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALL)


class DependencyInstallTests(unittest.TestCase):
	def test_archive_verification_before_extraction(self):
		with tempfile.TemporaryDirectory(prefix='dependency source ') as temporary:
			root = Path(temporary)
			source = root / 'source.tar.gz'
			content = b'synthetic dependency source\n'

			with tarfile.open(source, 'w:gz') as archive:
				member = tarfile.TarInfo('package/source.txt')
				member.size = len(content)
				archive.addfile(member, io.BytesIO(content))

			digest = hashlib.sha256(source.read_bytes()).hexdigest()
			pin = ('1.0', source.name, digest, source.as_uri())
			destination = root / 'extracted'
			INSTALL.fetch_source(pin, root / 'downloads', destination)
			self.assertEqual((destination / 'source.txt').read_bytes(), content)

			rejected = root / 'rejected'
			bad_pin = ('1.0', source.name, hashlib.sha256(b'wrong').hexdigest(),
				source.as_uri())

			with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
				INSTALL.fetch_source(bad_pin, root / 'downloads', rejected)

			self.assertFalse(rejected.exists())

	def test_download_failure_does_not_start_builder(self):
		for platform, name in (('linux', 'opus'), ('windows', 'vpx'),
			('wasm', 'sdl'), ('wasm', 'zlib')):
			with self.subTest(platform=platform, name=name), \
				patch.object(INSTALL, 'fetch_source',
				side_effect=ValueError('unverified source')), \
				patch.object(INSTALL.subprocess, 'run') as build:
				with self.assertRaisesRegex(ValueError, 'unverified source'):
					INSTALL.install_dependency(platform, name, ())

				build.assert_not_called()


if __name__ == '__main__':
	unittest.main()
