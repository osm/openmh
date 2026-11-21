#!/usr/bin/env python3
"""Exercise patched sensitivity rows, saved profiles and visible slider changes."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.live.check_menu_controls_live import action_row, snapshot
from tests.checks.support.application import frontend
from tests.checks.support.images import bmp_rgb
from tests.checks.support.menu import menu_key
from tests.checks.support.x11 import xdo


def hold(button, seconds):
	xdo('keydown',button)

	try:
		time.sleep(seconds)
	finally:
		xdo('keyup',button)

	time.sleep(.15)


def run(args, root):
	assert action_row(args.gamedir) == 4, 'requires patched Controls artwork'
	cfg, log, controls = root/'test.cfg', root/'live.log', root/'Controls'
	controls.mkdir()
	profile = controls/'Keyboard.clo'
	profile.write_bytes(b'LayoutName Keyboard\nMouseSpeedX 1.25\nMouseSpeedY 2.5\n'
		b'MouseSpeedZ 0.75\nForceFeedback On\n')
	write_cfg(cfg,dict(ControlName=b'Keyboard',SFXVolume=b'0',MusicVolume=b'0',MenuConfirmation=b'Off'))
	env = dict(os.environ,SDL_VIDEO_DRIVER='x11',SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'),'--debug-no-intro', '--debug-skip-loading-wait', '1','--assets',str(args.gamedir),
		'--debug-settings',str(cfg),'--debug-controls',str(controls),'--audio', '0','--debug-no-records',
		'--debug-recordings',str(root/'Recordings'),'--debug-page','controls', '--debug-window-titles', '1']
	source = snapshot(args.gamedir/'Game')

	def capture(name):
		image = root/(name+'.bmp')
		result = subprocess.run(command+['--debug-selection','1','--debug-frames','120','--debug-capture',str(image)],
			env=env,capture_output=True,timeout=60)
		assert result.returncode == 0,result

		return image,bmp_rgb(image)

	_, before_pixels = capture('before')

	with frontend(command,[],log,env) as (_, menu):
		menu_key(menu,'Right')  # Enter the selected profile's bindings column.
		menu_key(menu,'Left')  # Feedback is now editable on the SDL provider.
		menu_key(menu,'Right')
		menu_key(menu,'Down')
		hold('Right',.35)
		menu_key(menu,'Down')
		hold('Left',.25)
		menu_key(menu,'Down')
		hold('Right',.2)
		menu_key(menu,'Escape')

	saved = read_cfg(profile)
	assert float(saved[b'MouseSpeedX']) > 1.25,saved
	assert .01 < float(saved[b'MouseSpeedY']) < 2.5,saved
	assert float(saved[b'MouseSpeedZ']) > .75 and saved[b'ForceFeedback'] == b'On',saved
	unchanged = profile.read_bytes()
	image, after_pixels = capture('after')
	assert profile.read_bytes() == unchanged
	changed = [i//3//640 for i,(a,b) in enumerate(zip(before_pixels,after_pixels)) if a != b]
	assert changed and min(changed) >= 112 and max(changed) < 184,(min(changed),max(changed))

	with frontend(command,[],log,env) as (_, menu):
		menu_key(menu,'Right')

		for _ in range(action_row(args.gamedir,2)):
			menu_key(menu,'Down')

		for button in ('5','4'):
			menu_key(menu,'Return')
			xdo('click',button)
			time.sleep(.4)
			menu_key(menu,'Down')

		menu_key(menu,'Escape')

	captured = read_cfg(profile)
	assert captured[b'Accelerate'] == b'MouseIn' and captured[b'Brake'] == b'MouseOut',captured
	assert all(captured[key] == saved[key] for key in
		(b'MouseSpeedX',b'MouseSpeedY',b'MouseSpeedZ',b'ForceFeedback'))

	with frontend(command,[],log,env) as (_, menu):
		menu_key(menu,'Right')
		menu_key(menu,'Left')
		menu_key(menu,'Escape')

	assert read_cfg(profile)[b'ForceFeedback'] == b'Off'

	with frontend(command,[],log,env) as (_, menu):
		menu_key(menu,'Right')
		menu_key(menu,'Right')
		menu_key(menu,'Escape')

	assert read_cfg(profile)[b'ForceFeedback'] == b'On'
	unchanged = profile.read_bytes()

	with frontend(command,[],log,env):
		pass

	assert profile.read_bytes() == unchanged and snapshot(args.gamedir/'Game') == source

	if args.capture_dir:
		args.capture_dir.mkdir(parents=True,exist_ok=True)
		(args.capture_dir/'controls-sensitivity.bmp').write_bytes(image.read_bytes())

	print('Patched Controls X11: X/Y/Z adjustments, visible sliders, wheel capture, save/restart and feedback Off/On persistence passed.')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	parser.add_argument('--capture-dir',type=Path)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-sensitivity-live-') as directory:
		run(args,Path(directory))


if __name__ == '__main__':
	main()
