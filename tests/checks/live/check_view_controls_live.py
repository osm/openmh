#!/usr/bin/env python3
"""Verify stored noneditable camera bindings through the actual frontend.

Run under xvfb-run. The C camera fixture separately checks simultaneous keys,
repeat events and listener behavior.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.live.check_menu_controls_live import snapshot
from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, ticks, wait, window_for
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def mode(log):
	lines = [line.split() for line in log.read_text(errors='replace').splitlines(keepends=True)
		if line.startswith('camera-frame ') and line.endswith('\n')]

	return int(lines[-1][5])


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-view-bindings-') as directory:
		root = Path(directory)
		controls = root/'Controls'
		controls.mkdir()
		cfg,log,profile = root/'race.cfg',root/'race.log',controls/'Keyboard.clo'
		write_cfg(cfg,dict(ControlName=b'Keyboard',PlayerName=b'Camera',TeamName=b'TEST',
			CarName=fields(args.gamedir/'Game/car01.car')['carname'].encode(),
			TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'].encode(),
			GameType=b'TimeAttack',TrackLaps=b'1',RecordRace=b'Off',MenuConfirmation=b'Off',
			SFXVolume=b'0',MusicVolume=b'0',DetailMode=b'Custom',ViewDistance=b'40',ScreenDimension=b'640 400'))
		write_cfg(profile,dict(LayoutName=b'Keyboard',InCarView=b'P',OutCarView=b'F',CameraView=b'G',
			CyclePlayers=b'N',RearView=b'B',GameMenu=b'V'))
		before = profile.read_bytes()
		source = snapshot(args.gamedir/'Game')
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1','--assets',str(args.gamedir),'--debug-settings',str(cfg),
			'--debug-controls',str(controls),'--debug-no-records','--audio', '0','--debug-recordings',str(root/'Recordings'),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

		for reload in (False,True):
			with frontend(command,['--debug-page','single'],log,env) as (process,menu):
				menu_key(menu,'Return')
				race = window_for(process,log,r'TIME ATTACK \| 1 cars')
				xdo('windowfocus','--sync',race)
				wait(process,log,lambda: ticks(log) > 1800)
				assert mode(log) == 1,diagnostic(log)

				# Old F1 has no fallback once the profile replaces it. The
				# replacement P must select cockpit without pausing the race.
				# Leave the restored opening cockpit before testing that F1
				# is inactive; otherwise an accidental F1 binding is hidden.
				key(race,'f',process,log)
				key(race,'F1',process,log)
				assert mode(log) == 0,diagnostic(log)

				for button,expected in (('p',1),('g',2),('f',0)):
					previous = ticks(log)
					key(race,button,process,log)
					wait_frames(process,log,frames(log),2)
					assert mode(log) == expected and ticks(log) > previous,diagnostic(log)

				key(race,'v',process,log)
				key(race,'Up',process,log)
				xdo('keydown','Return')

				try:
					wait(process,log,lambda: 'Frontend resumed: main' in log.read_text())
				finally:
					xdo('keyup','Return')

			assert profile.read_bytes() == before and read_cfg(cfg)[b'ControlName'] == b'Keyboard'
			assert snapshot(args.gamedir/'Game') == source
			print(f'Camera bindings {"reloaded" if reload else "loaded"}: CLO -> frontend -> race, '
				'old F1 inactive, P/F/G select views without shortcut conflicts, clean return passed',flush=True)


if __name__ == '__main__':
	main()
