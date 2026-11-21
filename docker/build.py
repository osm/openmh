#!/usr/bin/env python3
"""Build and export OpenMH using the selected container toolchain."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess

from export_sources import export_notices, export_sources
from runtime_linux import export_runtime as export_linux
from runtime_wasm import export_runtime as export_wasm
from runtime_windows import export_runtime as export_windows


def run(*args):
	subprocess.run([str(arg) for arg in args], check=True)


def build_wasm(output, options):
	export_notices(output, 'wasm')
	run('bash', 'scripts/build', 'all', '--platform', 'wasm', '-G', 'Ninja',
		'-DCMAKE_PREFIX_PATH=/opt/wasm', '-DCMAKE_FIND_ROOT_PATH=/opt/wasm',
		'-DZLIB_ROOT=/opt/wasm', '-DZLIB_USE_STATIC_LIBS=ON',
		*options, f'-DMH_WASM_LICENSE_NOTICES={output}/licenses.txt')
	export_wasm(Path('build/wasm'), output)

	return Path('build/wasm')


def build_applications(platform, options):
	application = os.environ.get('APPLICATIONS', 'all')

	if application not in ('all', 'client', 'server', 'extractor'):
		raise ValueError('APPLICATIONS must be all, client, server or extractor')

	directory = Path('build') / platform
	defaults = [
		f'-DCMAKE_PREFIX_PATH=/opt/{platform}',
		f'-DOPENSSL_ROOT_DIR=/opt/{platform}',
		'-DOPENSSL_USE_STATIC_LIBS=ON',
		f'-DZLIB_ROOT=/opt/{platform}',
		'-DZLIB_USE_STATIC_LIBS=ON',
	]
	run('bash', 'scripts/build', application, '--platform', platform,
		*defaults, *options)

	return directory


def build_linux(output, options):
	directory = build_applications('linux', options)
	export_linux(directory, output)
	export_notices(output, 'linux')

	return directory


def build_windows(output, options):
	os.environ['PKG_CONFIG_LIBDIR'] = '/opt/windows/lib/pkgconfig'
	defaults = [
		'-DCMAKE_FIND_ROOT_PATH=/opt/windows',
		'-DCMAKE_EXE_LINKER_FLAGS=-static',
	]
	directory = build_applications('windows', [*defaults, *options])
	export_windows(directory, output)
	export_notices(output, 'windows')

	return directory


BUILDERS = {
	'linux': build_linux,
	'windows': build_windows,
	'wasm': build_wasm,
}


def build(platform):
	options = [
		'-DMH_ENABLE_WEBRTC=' + os.environ.get('MH_ENABLE_WEBRTC', 'ON'),
		*shlex.split(os.environ.get('CMAKE_ARGS', '')),
	]
	output = Path('/out') / platform
	output.mkdir(parents=True)
	directory = BUILDERS[platform](output, options)
	export_sources(output, platform)
	run('bash', 'scripts/release', '--platform', platform, '--build', directory,
		'--runtime', output, '--output', output)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('platform', choices=BUILDERS)
	args = parser.parse_args()
	build(args.platform)


if __name__ == '__main__':
	main()
