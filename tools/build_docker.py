#!/usr/bin/env python3
"""Build release bundles in Docker and replace each platform's output cleanly."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {'linux_amd64': 'linux', 'windows_amd64': 'windows', 'wasm32': 'wasm'}


def check_destination(destination):
	if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
		raise ValueError(f'Expected a regular output directory: {destination}')

	previous = destination.with_name('.' + destination.name + '_previous')

	if previous.exists() or previous.is_symlink():
		raise ValueError(f'Previous output needs recovery before rebuilding: {previous}')

	return previous


def replace_output(source, destination):
	previous = check_destination(destination)
	had_output = destination.exists()

	if had_output:
		destination.rename(previous)

	try:
		source.rename(destination)
	except OSError:
		if had_output:
			previous.rename(destination)

		raise

	if had_output:
		shutil.rmtree(previous)


def build(target, build_args=(), no_cache=False, cache_from=(), cache_to=()):
	platforms = list(TARGETS.values()) if target == 'all' else [TARGETS[target]]
	output = ROOT / 'build/packages'

	for platform in platforms:
		check_destination(output / platform)

	output.mkdir(parents=True, exist_ok=True)

	# Export separately so failed builds cannot damage the previous release.
	with tempfile.TemporaryDirectory(prefix='.docker_', dir=output) as temporary:
		exported = Path(temporary)
		command = ['docker', 'buildx', 'build', '--platform', 'linux/amd64',
			'-f', str(ROOT / 'docker/Dockerfile'),
			'--target', 'all' if target == 'all' else platforms[0],
			'--output', f'type=local,dest={exported}']

		for argument in build_args:
			command.extend(['--build-arg', argument])

		for source in cache_from:
			command.extend(['--cache-from', source])

		for destination in cache_to:
			command.extend(['--cache-to', destination])

		if no_cache:
			command.append('--no-cache')

		subprocess.run([*command, str(ROOT)], check=True, cwd=ROOT)

		for platform in platforms:
			source = exported / platform

			if source.is_symlink() or not source.is_dir() or not any(source.iterdir()):
				raise ValueError(f'Docker did not export the {platform} bundle')

		for platform in platforms:
			replace_output(exported / platform, output / platform)
			print(f'Release bundle: {output / platform}', flush=True)


def main(target, arguments):
	parser = argparse.ArgumentParser(prog=f'scripts/build_docker_{target}', description=__doc__)
	parser.add_argument('--build-arg', action='append', default=[], metavar='NAME=VALUE',
		help='Docker build argument, repeatable (for example MH_ENABLE_WEBRTC=OFF)')
	parser.add_argument('--no-cache', action='store_true',
		help='rebuild Docker layers, including dependencies, without cached results')
	parser.add_argument('--cache-from', action='append', default=[], metavar='SPEC',
		help='Buildx cache source, repeatable')
	parser.add_argument('--cache-to', action='append', default=[], metavar='SPEC',
		help='Buildx cache destination, repeatable')
	args = parser.parse_args(arguments)

	if target not in (*TARGETS, 'all'):
		parser.error('Unknown Docker build target')

	try:
		build(target, args.build_arg, args.no_cache, args.cache_from, args.cache_to)
	except (ValueError, OSError, subprocess.CalledProcessError) as error:
		parser.exit(1, f'Build failed: {error}\n')


if __name__ == '__main__':
	main(sys.argv[1] if len(sys.argv) > 1 else 'all', sys.argv[2:])
