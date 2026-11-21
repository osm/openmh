#!/usr/bin/env python3
"""Exercise demo/ghost/network/league settings through real X11 menu input.

Run under xvfb-run with a Vulkan-capable software driver and xdotool.
All settings/logs are temporary; the network case hosts one local UDP player.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time


from tests.checks.support.x11 import close_window, xdo, frames, wait_frames, key


from tests.checks.support.process import COMMAND_TIMEOUT


def drive(arguments, root, label, edit, viewport=None):
	log = root/(label+'.log')
	environment = dict(os.environ,SDL_VIDEO_DRIVER='x11',SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as stream:
		process = subprocess.Popen(arguments + ['--debug-camera-trace', '1',
			'--debug-window-titles', '1'],stdout=stream,stderr=stream,env=environment,cwd=root)

		try:
			deadline = time.monotonic()+45
			window = None

			while time.monotonic() < deadline:
				assert process.poll() is None,(arguments,log.read_text())
				found = subprocess.run(['xdotool','search','--onlyvisible','--pid',str(process.pid)],
					capture_output=True,text=True, timeout=COMMAND_TIMEOUT)

				if found.returncode == 0 and found.stdout.strip():
					window = int(found.stdout.splitlines()[0])
					break

				time.sleep(.1)

			assert window is not None,(arguments,log.read_text())

			# SDL creates the window before loading/uploading the race assets.
			# The renderer replaces its initial title after the first frame.
			while ' | ' not in xdo('getwindowname',window):
				assert process.poll() is None and time.monotonic() < deadline,(arguments,log.read_text())
				time.sleep(.1)

			xdo('windowfocus','--sync',window)
			wait_frames(process,log,frames(log),6)  # Original startup suppresses four input polls.

			if viewport is not None:
				geometry = dict(line.split('=',1) for line in xdo('getwindowgeometry','--shell',window).splitlines())
				assert (int(geometry['WIDTH']),int(geometry['HEIGHT'])) == viewport,geometry
				assert f'Race viewport: {viewport[0]}x{viewport[1]}' in log.read_text(),log.read_text()

			if edit:
				# Main -> Graphics -> View distance; decrement once.
				for name in ('Escape','Down','Down','Return','Down','Down','Left','Escape','Escape'):
					key(window,name,process,log)

			close_window(window)
			assert process.wait(timeout=30) == 0,(arguments,log.read_text())
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--mode',action='append',choices=('demo','ghost','network','league'))
	args = parser.parse_args()
	args.gamedir = args.gamedir.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-session-settings-') as temporary:
		root = Path(temporary)

		for mode in args.mode or ('demo','ghost','network','league'):
			viewport = dict(demo=(320,200),ghost=(854,481),network=(1280,720),league=(800,600))[mode]
			cfg = root/(mode+'.cfg')
			cfg.write_text('CustomCarColours On\nCarColour1 0 255 0\nCarColour2 0 255 0\n'
				'CarColour3 0 255 0\nPlayerColour 18 52 86\nViewDistance 100\n'
				'SFXVolume 37\nMusicVolume 42\nSessionPassword PreserveMe\n')
			cfg.write_text(cfg.read_text()+f'ScreenDimension {viewport[0]} {viewport[1]}\n')

			if mode == 'network':
				command = [str(build_executable(args.build, 'openmotor')), '--debug-network','--debug-terminal-lobby','--debug-host','127.0.0.1','--debug-players','1',
					'--debug-port',str(45000+os.getpid()%10000),'--debug-wait','20','--debug-no-records',
					'--debug-settings',str(cfg),'--debug-volume','20','--debug-music-volume','30', '--debug-window-titles', '1', '--assets', str(args.gamedir)]
			elif mode == 'league':
				league = root/'Settings.lgf'
				cfg.write_text(cfg.read_text()+'PlayerName CFG driver\nTeamName NOW\n'
					'CarDetail Low\nTrackDetail Medium\n')
				command = [str(build_executable(args.build, 'openmotor')), '--debug-league','--debug-save',str(league),
					'--debug-new','Settings','--debug-player','Saved driver','--debug-no-records',
					'--debug-settings',str(cfg),'--debug-mute', '--debug-window-titles', '1', '--assets', str(args.gamedir)]
				initial = cfg.read_bytes()
				subprocess.run(command+['--debug-check'],check=True,capture_output=True,timeout=60,cwd=root)
				assert not league.exists() and cfg.read_bytes() == initial,'league --check wrote a file'
				environment = dict(os.environ)
				subprocess.run(command[:command.index('--debug-settings')]+['--debug-check',
					'--assets',str(args.gamedir)],check=True,
					capture_output=True,timeout=60,cwd=root,env=environment)
				assert not (root/'userdata/motorhead.cfg').exists(),'league --check opened default preferences'
			else:
				command = [str(build_executable(args.build, 'openmotor')), '--debug-demo',str(args.gamedir / 'Demos/JeePen.MDE'),
					'--debug-settings',str(cfg),'--debug-mute', '--debug-window-titles', '1', '--assets', str(args.gamedir)]

				if mode == 'ghost':
					command += ['--debug-ghost']

			drive(command,root,mode,True,viewport)
			saved = cfg.read_bytes()
			fields = {parts[0]:parts[1:] for line in saved.decode().splitlines() if (parts := line.split())}
			assert float(fields['ViewDistance'][0]) == 95,(mode,fields['ViewDistance'])
			assert fields['CustomCarColours'] == ['On'] and fields['CarColour1'] == ['0','255','0']
			assert fields['SessionPassword'] == ['PreserveMe']
			assert tuple(map(int,fields['ScreenDimension'])) == viewport
			assert float(fields['SFXVolume'][0]) == (20 if mode == 'network' else 0)
			assert float(fields['MusicVolume'][0]) == (29.8 if mode == 'network' else 0)

			if mode == 'league':
				league_bytes = league.read_bytes()
				decoded = subprocess.check_output([str(build_executable(args.build, 'mh-tbl-decode')),str(league),'--key',league.name],text=True, timeout=COMMAND_TIMEOUT)
				assert 'RaceNumber\t0\n' in decoded
				human = decoded.split('HumanPlayer\t',1)[1].split('\nAIPlayer\t',1)[0].split('\n\n',1)[0]
				assert human.startswith('Saved driver\n') and 'Colour\t18 52 86\n' in human
				assert fields['CarDetail'] == ['Low'] and fields['TrackDetail'] == ['Medium']
				assert fields['TeamName'] == ['NOW'] and fields['PlayerName'] == ['CFG','driver']
				duplicate = subprocess.run(command,capture_output=True,timeout=60,cwd=root)
				assert duplicate.returncode != 0 and league.read_bytes() == league_bytes
				start = command.index('--debug-new')
				del command[start:start+2]

				# Resuming must retain league identity even after CFG identity changes.
				changed = saved.replace(b'PlayerColour\t18 52 86\n',b'PlayerColour\t1 2 3\n')
				assert changed != saved
				saved = changed
				cfg.write_bytes(saved)

			drive(command,root,mode+'-reload',False,viewport)
			assert cfg.read_bytes() == saved,(mode,'unchanged reload rewrote preferences')

			if mode == 'league':
				assert league.read_bytes() == league_bytes,'unfinished league reload awarded or rewrote the save'
				no_settings = command[:command.index('--debug-settings')]+['--debug-no-settings','--debug-mute',
					'--assets',str(args.gamedir)]
				drive(no_settings,root,mode+'-disabled',False,(960,640))
				assert cfg.read_bytes() == saved and league.read_bytes() == league_bytes
				conflict = subprocess.run(command+['--debug-no-settings'],capture_output=True,timeout=60,cwd=root)
				assert conflict.returncode != 0 and cfg.read_bytes() == saved and league.read_bytes() == league_bytes
				print('league: read-only diagnostics, original marker metadata, duplicate-new rejection and unawarded-save preservation passed',flush=True)

			print(mode+f': X11 {viewport[0]}x{viewport[1]}, menu edit/save/reload, paint and CLI volume precedence passed',flush=True)


if __name__ == '__main__':
	main()
