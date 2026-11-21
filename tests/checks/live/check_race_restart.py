"""Start, restart and leave an eight-car native race with graphics and audio under X11."""

import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.support.application import frontend
from tests.checks.support.build import build_executable
from tests.checks.support.menu import diagnostic, race_and_return
from tests.checks.support.settings import encode


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True, help='native client build with MH_DEBUG=ON')
	parser.add_argument('--assets', type=Path, required=True, help='retail game directory or package')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-race-restart-') as temporary:
		root = Path(temporary)
		cfg = root / 'settings.cfg'
		log = root / 'race.log'
		cfg.write_bytes(encode(dict(Renderer='OpenMH', ScreenDimension='640 480',
			MenuConfirmation='Off', RecordRace='Off', SFXVolume='35', MusicVolume='0')))
		command = [str(build_executable(args.build, 'openmotor')), '--assets', str(args.assets),
			'--user-data', str(root / 'userdata'), '--debug-settings', str(cfg),
			'--debug-no-records', '--debug-no-intro', '--debug-skip-loading-wait', '1',
			'--debug-require-audio', '--debug-camera-trace', '1']
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		with frontend(command, ['--debug-page', 'graphics'], log, env) as (process, menu):
			race_and_return(process, menu, log, 'OpenMH', restart=True)
			assert 'Race audio:' in log.read_text(), diagnostic(log)

		print('OpenMH eight-car race with audio restarted, returned to the menu and exited cleanly.')


if __name__ == '__main__':
	main()
