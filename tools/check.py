"""Run development checks, including sanitizer tests and parser fuzzing."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CHECKS = ('constants', 'format', 'unit', 'memory')


def run(label, command):
	print(f'\n{label}', flush=True)
	subprocess.run(command, cwd=ROOT, check=True)


def unit_checks(build, config, jobs):
	python = [sys.executable, '-B']
	run('Running Python unit tests', python +
		['-m', 'unittest', 'discover', '-s', 'tests/python'])

	configure = ['cmake', '-S', str(ROOT), '-B', str(build),
		'-DBUILD_TESTING=ON', f'-DCMAKE_BUILD_TYPE={config}']

	run('Configuring unit tests', configure)
	run('Building unit tests', ['cmake', '--build', str(build),
		'--config', config, '--parallel', str(jobs)])
	run('Running CTest', ['ctest', '--test-dir', str(build),
		'--build-config', config, '--output-on-failure', '--no-tests=error',
		'--parallel', str(jobs)])


def selected_checks(checks, build, config, jobs):
	python = [sys.executable, '-B']

	if 'constants' in checks:
		run('Checking constants', python + [str(ROOT / 'tools/check_constants.py')])

	if 'format' in checks:
		run('Checking formatting', python + [str(ROOT / 'tools/format.py'), '--check'])

	if 'unit' in checks:
		unit_checks(build, config, jobs)

	if 'memory' in checks:
		run('Checking memory', [str(ROOT / 'scripts/check_memory')])


def main(arguments=None):
	arguments = list(sys.argv[1:] if arguments is None else arguments)

	if arguments and arguments[0] == 'integration':
		# Preserve the caller's directory for paths passed to integration checks.
		return subprocess.call([sys.executable, '-B', str(ROOT / 'tests/check.py'),
			*arguments[1:]])

	parser = argparse.ArgumentParser(prog='scripts/check', description=__doc__,
		formatter_class=argparse.RawDescriptionHelpFormatter,
		epilog='''With no selection flags, run constants, formatting, unit and memory checks.
Selection flags can be combined, for example: scripts/check --format --constants.
A fresh test build includes the client and needs no game assets.
Existing build directories retain their selected modules and compiler.
Requires CMake, a C compiler, development libraries and clang-format 18+.
Memory checks require Clang with sanitizer and libFuzzer runtimes.

Optional integration checks:
  scripts/check integration --list
  scripts/check integration CHECK --help
''')
	parser.add_argument('--build', type=Path, default=ROOT / 'build/checks/unit',
		help='test build directory (default: build/checks/unit)')
	parser.add_argument('--config', default='Debug', help='build configuration')
	parser.add_argument('--jobs', type=int,
		default=os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', '4'),
		help='parallel build/test jobs (default: CMAKE_BUILD_PARALLEL_LEVEL or 4)')
	selection = parser.add_argument_group('check selection (default: all)')
	selection.add_argument('--constants', dest='checks', action='append_const',
		const='constants', help='check source constants')
	selection.add_argument('--format', dest='checks', action='append_const',
		const='format', help='verify source formatting without changes')
	selection.add_argument('--unit', dest='checks', action='append_const',
		const='unit', help='run Python unit tests and build/run CTest')
	selection.add_argument('--memory', dest='checks', action='append_const',
		const='memory', help='run sanitizer tests and parser fuzzing')
	args = parser.parse_args(arguments)

	if args.jobs < 1:
		parser.error('--jobs must be positive')

	try:
		selected_checks(args.checks or CHECKS, args.build.resolve(),
			args.config, args.jobs)
	except (OSError, subprocess.CalledProcessError) as error:
		parser.exit(1, f'Checks failed: {error}\n')

	print('\nSelected checks passed.')

	return 0


if __name__ == '__main__':
	raise SystemExit(main())
