#!/usr/bin/env python3
"""Exercise CLO catalog reloads and actual changed bindings between races.

Run under xvfb-run. Only private CFG/CLO files are edited; installed assets
remain unchanged. Also checks the explicit session-only no-settings policy.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.live.check_menu_controls_live import bind, snapshot
from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, ticks, wait, window_for
from tests.checks.support.x11 import frames, key, wait_frames, xdo
from tests.checks.live.check_view_controls_live import mode


def launch(process, menu, log, first=False, cameras=False):
	previous = frames(log)

	if not first:
		menu_key(menu,'Return')

	menu_key(menu,'Return')
	race = window_for(process,log,r'TIME ATTACK \| 1 cars')
	xdo('windowfocus','--sync',race)
	wait_frames(process,log,max(previous,frames(log)),6)

	if cameras:
		wait(process,log,lambda:ticks(log) > 1800)

	return race


def leave(process, race, log):
	returned = log.read_text().count('Frontend resumed: main')
	key(race,'v',process,log)
	key(race,'Up',process,log)
	xdo('keydown','Return')

	try:
		wait(process,log,lambda:log.read_text().count('Frontend resumed: main') > returned)
	finally:
		xdo('keyup','Return')


def profile(path, name, inside, outside, track):
	write_cfg(path,dict(LayoutName=name,InCarView=inside,OutCarView=outside,CameraView=track,GameMenu=b'V'))


def persistent(args, root):
	root.mkdir()
	controls = root/'userdata'
	controls.mkdir(parents=True)
	cfg,log = root/'test.cfg',root/'live.log'
	keyboard,added = controls/'Keyboard.clo',controls/'000Fresh.clo'

	# Use keys without standalone pause/reset/debug shortcuts, so an old
	# binding can be tested as inactive without changing another race state.
	profile(keyboard,b'Keyboard',b'J',b'K',b'L')
	original_keyboard = keyboard.read_bytes()
	write_cfg(cfg,dict(ControlName=b'Keyboard',PlayerName=b'Reload',TeamName=b'TEST',
		CarName=fields(args.gamedir/'Game/car01.car')['carname'].encode(),
		TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'].encode(),
		GameType=b'TimeAttack',TrackLaps=b'1',RecordRace=b'Off',MenuConfirmation=b'Off',
		SFXVolume=b'0',MusicVolume=b'0',DetailMode=b'Custom',ViewDistance=b'40',ScreenDimension=b'640 400'))
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'),'--debug-no-intro', '--debug-skip-loading-wait', '1','--assets',str(args.gamedir),
		'--debug-settings',str(cfg),'--debug-no-records','--audio', '0','--debug-recordings',str(root/'Recordings'),
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with frontend(command,['--debug-page','single'],log,env) as (process,menu):
		race = launch(process,menu,log,first=True,cameras=True)

		for button,expected in (('k',0),('j',1)):
			key(race,button,process,log)
			assert mode(log) == expected,diagnostic(log)

		# Introduce a profile absent from the opening catalog, and select it
		# in the real CFG while the first race owns its old applied bindings.
		profile(added,b'Reloaded',b'T',b'Y',b'U')
		values = {name.decode('ascii'):value for name,value in read_cfg(cfg).items()}
		values['ControlName'] = b'Reloaded'
		write_cfg(cfg,values)
		before = cfg.read_bytes()
		leave(process,race,log)
		assert cfg.read_bytes() == before
		race = launch(process,menu,log,cameras=True)
		assert read_cfg(cfg)[b'ControlName'] == b'Reloaded'

		for button,expected in (('y',0),('j',0),('t',1),('u',2)):
			key(race,button,process,log)
			assert mode(log) == expected,diagnostic(log)

		# Reusing the same display name must still reread changed CLO bytes.
		profile(added,b'Reloaded',b'I',b'O',b'B')
		leave(process,race,log)
		race = launch(process,menu,log,cameras=True)

		for button,expected in (('o',0),('t',0),('i',1),('b',2)):
			key(race,button,process,log)
			assert mode(log) == expected,diagnostic(log)

		added.unlink()
		leave(process,race,log)

		# The removed selection falls back to the first fresh catalog entry.
		for button in ('Right','Right','Right','Return','Right','Right','Return'):
			menu_key(menu,button)

	sources = sorted((p for p in (args.gamedir/'Game').iterdir() if p.suffix.lower() == '.clo'),
		key=lambda p:p.name.lower())
	selected = read_cfg(sources[0])[b'LayoutName'].rstrip(b'\r')
	assert read_cfg(cfg)[b'ControlName'] == selected,(read_cfg(cfg),selected,diagnostic(log))
	assert f'profile 0/{len(sources)},' in log.read_text(),diagnostic(log)
	assert keyboard.read_bytes() == original_keyboard and not added.exists()
	print('Control reload: new profile/CFG selection, changed bindings in three races, removed-profile fallback and untouched source files passed',flush=True)


def transient(args, root):
	root.mkdir()
	log = root/'live.log'
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'),'--debug-no-intro', '--debug-skip-loading-wait', '1','--assets',str(args.gamedir),
		'--debug-no-settings','--debug-mode','2','--debug-confirmation','0','--debug-record','0','--debug-car','1','--debug-track','1',
		'--debug-laps','1','--debug-no-records','--audio', '0','--debug-recordings',str(root/'Recordings'),
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with frontend(command,['--debug-page','controls'],log,env) as (process,menu):
		menu_key(menu,'Right')

		for _ in range(9):
			menu_key(menu,'Down')

		bind(menu,'v')
		menu_key(menu,'Escape')
		menu_key(menu,'Escape')

		for _ in range(2):
			race = launch(process,menu,log)
			leave(process,race,log)

	assert not list(root.rglob('*.clo')) and not list(root.rglob('*.cfg'))
	assert not list((root/'userdata').glob('*.clo'))
	print('Control reload: session-only V pause binding survives two race returns without CFG/CLO writes',flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	args = parser.parse_args()
	source = snapshot(args.gamedir/'Game')

	with tempfile.TemporaryDirectory(prefix='mh-control-reentry-') as directory:
		root = Path(directory)
		persistent(args,root/'persistent')
		transient(args,root/'transient')

	assert snapshot(args.gamedir/'Game') == source


if __name__ == '__main__':
	main()
