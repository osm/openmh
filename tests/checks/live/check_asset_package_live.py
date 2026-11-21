#!/usr/bin/env python3
"""Exercise package discovery, race loading, saves and optional opening movies.

Requires a client configured with -DMH_DEBUG=ON and the software renderer.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from tests.checks.live.check_audio_toggle_live import race_audio
from tests.checks.live.check_opening_movies_live import launch, menu, movie
from tests.checks.support.build import build_executable
from tests.checks.support.menu import menu_key
from tests.checks.support.settings import read_cfg


def menu_catalog(command, selection, root, env):
	result = subprocess.run(command + selection + ['--audio', '0', '--debug-frames', '2'],
		cwd=root.parent, env=env, capture_output=True,
		text=True, timeout=30)
	assert result.returncode == 0, result.stdout + result.stderr
	assert 'Menu preview:' in result.stdout, result.stdout + result.stderr

	return next(line for line in result.stdout.splitlines() if line.startswith('Catalog:'))


def check_demo_selection(command, source, root, env, retail_catalog):
	demo = root / 'demo.ass'
	shutil.copyfile(source, demo)
	original = hashlib.sha256(demo.read_bytes()).digest()
	selected = menu_catalog(command, ['--assets', str(demo)], root, env)
	assert selected != retail_catalog, 'explicit demo selection loaded the default retail package'
	assert menu_catalog(command, [], root, env) == retail_catalog
	assert hashlib.sha256(demo.read_bytes()).digest() == original
	print('Named demo and default retail package coexist and select different catalogs.', flush=True)


def check_user_data_selection(command, package, root, env):
	first = root / 'profiles' / 'Player One'
	second = root / 'profiles' / 'Player Two'

	# menu_catalog runs from root.parent, so this also tests relative paths.
	first_selection = ['--assets', str(package), '--user-data', str(first.relative_to(root.parent))]
	menu_catalog(command + ['--debug-name', 'ProfileOne'], first_selection, root, env)
	first_cfg = first / 'motorhead.cfg'
	saved = first_cfg.read_bytes()
	menu_catalog(command + ['--debug-name', 'ProfileTwo'],
		['--assets', str(package), '--user-data', str(second)], root, env)
	assert first_cfg.read_bytes() == saved, 'second profile changed the first profile'
	assert read_cfg(first_cfg)['PlayerName'] == 'ProfileOne'
	assert read_cfg(second / 'motorhead.cfg')['PlayerName'] == 'ProfileTwo'
	menu_catalog(command, first_selection, root, env)
	assert read_cfg(first_cfg)['PlayerName'] == 'ProfileOne'
	assert not (first / 'userdata').exists(), 'override appended an extra userdata directory'
	assert not (root / 'userdata').exists(), 'override wrote to the default directory'

	print('Custom absolute/relative user-data roots isolate and reload profiles.', flush=True)


def check_invalid_user_data(command, package, root, env):
	blocked = root / 'not-a-directory'
	blocked.write_bytes(b'keep this file')
	result = subprocess.run(command + ['--assets', str(package), '--user-data', str(blocked),
		'--audio', '0', '--debug-frames', '2'],
		cwd=root.parent, env=env, capture_output=True, text=True, timeout=30)
	assert result.returncode != 0, result.stdout + result.stderr
	assert blocked.read_bytes() == b'keep this file'
	assert not (root / 'userdata').exists(), 'invalid override fell back to the default directory'
	print('Invalid user-data roots fail without fallback or overwriting existing files.', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--package', type=Path, required=True)
	parser.add_argument('--movies', action='store_true')
	parser.add_argument('--demo-package', type=Path, help='Also select a demo beside the retail package')
	args = parser.parse_args()

	client = build_executable(args.build, 'openmotor')
	help_text = subprocess.check_output([client, '--help'], text=True)

	if '--debug-no-intro' not in help_text:
		parser.error('Configure the client with -DMH_DEBUG=ON for this check')

	with tempfile.TemporaryDirectory(prefix='mh-package-live-') as temporary:
		root = Path(temporary)
		package = root / 'openmotor.ass'
		shutil.copyfile(args.package, package)
		executable = root / 'openmotor'
		shutil.copy2(client, executable)
		original = hashlib.sha256(package.read_bytes()).digest()
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(executable), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--renderer', 'software',
			'--debug-no-records']
		check_user_data_selection(command, package, root, env)
		check_invalid_user_data(command, package, root, env)

		# Run from another directory: discovery must use the executable path.
		for selection in ([], ['--assets', str(root)], ['--assets', str(package)]):
			catalog = menu_catalog(command, selection, root, env)

		if args.demo_package:
			check_demo_selection(command, args.demo_package, root, env, catalog)

		assert (root / 'userdata' / 'motorhead.cfg').is_file()
		print('Beside-executable, directory and explicit package selection all reach the menu and save.', flush=True)
		race_audio(command, env, root, 1)

		if args.movies:
			movie_args = argparse.Namespace(build=args.build, gamedir=package, renderer='software')

			with launch(movie_args, root, 'movie') as (process, log, cfg):
				window = movie(process, log, 0)
				menu_key(window, 'Escape')
				menu(process, log)

			assert cfg.exists()
			print('Packaged opening movie -> menu handoff passes.', flush=True)

		assert hashlib.sha256(package.read_bytes()).digest() == original
		print('Package bytes unchanged after gameplay and saves.', flush=True)


if __name__ == '__main__':
	main()
