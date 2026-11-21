#!/usr/bin/env python3
"""Exercise Main/Options/Graphics, CFG edits and save-before-preset under X11.

Uses installed assets, isolated user storage, dummy audio and real key events.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.images import bmp_rgb
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.input import held
from tests.checks.support.x11 import frames, key, wait_frames, xdo


from tests.checks.support.application import frontend


def run(args, root):
	cfg = root / 'test.cfg'
	log = root / 'live.log'
	base = dict(PlayerName='Graphics driver', TeamName='NOW',
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'],
		GameType='Single', TrackLaps='1', RecordRace='Off', MenuConfirmation='Off',
		DetailMode='Custom', ViewDistance='100', SFXVolume='0', MusicVolume='0',
		ScreenDimension='640 400')
	cfg.write_bytes(encode(base))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir),
		'--renderer', args.renderer, '--grab-mouse', '0',
		'--debug-settings', str(cfg), '--debug-recordings', str(root / 'recordings'), '--debug-no-records', '--audio', '0',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1', '--debug-selection', '2']

	# Keep pointer motion out of this keyboard-navigation fixture.
	xdo('mousemove', '0', '0')

	with frontend(command, [], log, env) as (process, menu):
		# Start on Rankings: the preceding Multi item may be hidden.
		for name in ('Right', 'Return', 'Right', 'Return', 'Down', 'Right'):
			menu_key(menu, name)

		# Pass presentation options and enable the FPS display.
		for row in range(6):
			menu_key(menu, 'Down')

			if row == 3:
				menu_key(menu, 'Right')  # Show FPS.

		held(menu, 'Left', .25)  # Brightness.

		for _ in range(3):
			menu_key(menu, 'Down')
			menu_key(menu, 'Left')

		menu_key(menu, 'Down')
		held(menu, 'Right', .3)  # Checkpoint delay.

		# Preset selection must not immediately destroy individual fields.
		for name in ('Down', 'Left', 'Right'):
			menu_key(menu, name)

		for _ in range(11):
			menu_key(menu, 'Down')
			menu_key(menu, 'Left')

		# Perspective correction/quality, distance and motion blur.
		for _ in range(2):
			menu_key(menu, 'Down')
			menu_key(menu, 'Right')

		menu_key(menu, 'Down')
		held(menu, 'Left', .25)
		menu_key(menu, 'Down')
		menu_key(menu, 'Right')

	edited = read_cfg(cfg)
	size = tuple(map(int, edited['ScreenDimension'].split()))
	assert size != (640, 400), edited
	brightness = list(map(float, edited['Brightness'].split()))
	assert 0 < brightness[0] < 100 and len(set(brightness)) == 1, edited
	assert 4 < float(edited['CheckpointDelay']) < 10 and 30 < float(edited['ViewDistance']) < 100, edited
	wanted = dict(ShowFPS='On', DetailMode='Custom', InfoDetail='Selective', RoadMap='Selective',
		CheckpointInfo='Selective', LensFlares='Off', Sparks='Off', Smoke='Off', Halos='Off',
		Shadow='Off', SkidMarks='Off', NamePlates='Flat', SkyImage='Off', TrackDetail='Medium',
		CarDetail='Medium', CarShading='Gouraud', PerspectiveOn='All', Perspective='Good', MotionBlur='On')
	assert all(edited.get(k) == v for k, v in wanted.items()), (edited, wanted)
	assert 'focus 22' in log.read_text() and 'page graphics' in log.read_text(), diagnostic(log)
	print('X11 Main -> Options -> Graphics: resolution, all Custom rows, held sliders and CFG save passed', flush=True)

	# A capture reloads the edited catalog choice and all 23 visible rows
	# without publishing either CFG or a preset.
	before = cfg.read_bytes()
	image = root / 'custom.bmp'
	result = subprocess.run(
		command + ['--debug-page', 'graphics', '--debug-selection', '22', '--debug-capture', str(image),
			'--debug-frames', '30'],
		env=env,
		capture_output=True,
		text=True,
		timeout=60,
	)
	assert result.returncode == 0 and f'size {size[0]}x{size[1]}, detail 4' in result.stdout, result
	assert cfg.read_bytes() == before and len(set(bmp_rgb(image))) > 32

	with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '21'], log, env) as (process, menu):
		xdo('windowfocus', '--sync', menu)
		xdo('keydown', 'Control_L')

		try:
			held(menu, 'Right', .8)
		finally:
			xdo('keyup', 'Control_L')

	assert float(read_cfg(cfg)['ViewDistance']) == 150, read_cfg(cfg)
	print('X11 Graphics reload, capture immutability and Control-held distance clamp passed', flush=True)

	# The saved dimensions need not be a monitor mode. Launching with Custom
	# preserves individual settings; selecting Max applies only after saving.
	for preset in (False, True):
		cfg.write_bytes(encode(dict(base, ScreenDimension='854 481', ViewDistance='40',
			LensFlares='Off', MotionBlur='On', Halos='Off', Shadow='Off')))

		with frontend(command, ['--debug-page', 'graphics', '--debug-selection', '7'], log, env) as (process, menu):
			if preset:
				menu_key(menu, 'Left')  # Custom -> Max.

			for name in ('Escape', 'Escape', 'Return', 'Return'):
				menu_key(menu, name)

			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)
			geometry = dict(line.split('=', 1) for line in xdo('getwindowgeometry', '--shell', race).splitlines())
			assert (int(geometry['WIDTH']), int(geometry['HEIGHT'])) == (854, 481), geometry
			assert 'Race viewport: 854x481' in log.read_text(), diagnostic(log)
			assert f'Race renderer: {"Software" if args.renderer == "software" else "Glide"}' in log.read_text()
			saved = read_cfg(cfg)
			assert saved['DetailMode'] == ('Max' if preset else 'Custom'), saved
			assert float(saved['ViewDistance']) == 40 and saved['LensFlares'] == 'Off' and saved['MotionBlur'] == 'On', saved

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			saved = read_cfg(cfg)
			assert float(saved['ViewDistance']) == 40, saved
			assert saved['LensFlares'] == 'Off' and saved['Halos'] == 'Off' and saved['MotionBlur'] == 'On', saved
			assert tuple(map(int, saved['ScreenDimension'].split())) == (854, 481), saved

		saved = read_cfg(cfg)
		assert float(saved['ViewDistance']) == (100 if preset else 40), saved
		assert saved['LensFlares'] == 'Off' and saved['Halos'] == 'Off' and saved['MotionBlur'] == 'On', saved
		print(f'X11 Graphics {"Max save-before-preset" if preset else "Custom preservation"}: '
			'854x481 race, abort and frontend return passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('glide', 'software'), default='glide')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-graphics-live-') as directory:
		run(args, Path(directory))


if __name__ == '__main__':
	main()
