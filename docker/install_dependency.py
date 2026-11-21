#!/usr/bin/env python3
"""Prepare a pinned dependency and build it for the selected container platform."""

import argparse
import hashlib
from pathlib import Path
import subprocess


RECIPES = Path(__file__).resolve().parent
DOWNLOAD_RETRIES = 3
HASH_CHUNK_BYTES = 1024 * 1024


def pinned_sources():
	sources = {}

	for row in (RECIPES / 'sources.txt').read_text().splitlines():
		if row.strip() and not row.lstrip().startswith('#'):
			name, version, archive, digest, url = row.split()
			sources[name] = (version, archive, digest, url)

	return sources


def fetch_source(source, downloads, directory):
	_, filename, expected, url = source
	downloads.mkdir(parents=True, exist_ok=True)
	archive = downloads / filename
	subprocess.run(['curl', '--fail', '--location', '--retry',
		str(DOWNLOAD_RETRIES), url, '--output', str(archive)],
		check=True)

	digest = hashlib.sha256()

	with archive.open('rb') as stream:
		for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b''):
			digest.update(chunk)

	if digest.hexdigest() != expected:
		raise ValueError(f'SHA-256 mismatch: {filename}')

	directory.mkdir(parents=True, exist_ok=True)
	subprocess.run(['tar', '-xf', str(archive), '--strip-components=1',
		'-C', str(directory)], check=True)


def install_dependency(platform, name, source):
	directory = Path('/deps') / name
	fetch_source(source, Path('/downloads'), directory)
	subprocess.run(['bash', str(RECIPES.parent / 'scripts' / 'build_dependency'),
		platform, name, str(directory), '/work/' + name,
		'/opt/' + platform], check=True)


def main():
	sources = pinned_sources()
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('platform', choices=('linux', 'windows', 'wasm'))
	parser.add_argument('name', choices=sources)
	args = parser.parse_args()

	if args.platform == 'wasm' and args.name == 'openssl':
		parser.error('OpenSSL is only built for Linux and Windows')

	try:
		install_dependency(args.platform, args.name, sources[args.name])
	except (ValueError, OSError, subprocess.CalledProcessError) as error:
		parser.exit(1, f'Dependency installation failed: {error}\n')


if __name__ == '__main__':
	main()
