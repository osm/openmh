#!/usr/bin/env python3
"""Select/save/reload native texture filters in the real Graphics menu (X11)."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.menu import menu_key, diagnostic
from tests.checks.support.settings import read_cfg


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-filter-live-') as tmp:
		root = Path(tmp)
		cfg, log = root / 'settings.cfg', root / 'menu.log'
		cfg.write_text('DetailMode Custom\nScreenDimension 640 400\nRenderer Glide\n')
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [build_executable(args.build, 'openmotor'), '--user-data', str(root / 'userdata'), '--assets', args.gamedir,
			'--debug-settings', cfg, '--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1', '--audio', '0', '--debug-window-titles', '1']
		command = [str(arg) for arg in command]

		for mode in ('Nearest', 'Bilinear', 'Trilinear'):
			with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '23'], log, env) as (_, window):
				menu_key(window, 'Down')
				menu_key(window, 'Right')

			assert 'focus 24' in log.read_text(), diagnostic(log)
			assert read_cfg(cfg)['TextureFiltering'] == mode, read_cfg(cfg)

		with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '0'], log, env) as (_, window):
			menu_key(window, 'Left')

		assert read_cfg(cfg)['Renderer'] == 'Software'

		with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '22'], log, env) as (_, window):
			menu_key(window, 'Down')

		assert 'focus 24' not in log.read_text(), diagnostic(log)
		assert read_cfg(cfg)['TextureFiltering'] == 'Trilinear'
		print('Graphics: filter navigation, all choices, persistence and software capability gate passed')


if __name__ == '__main__':
	main()
