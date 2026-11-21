#!/usr/bin/env python3
"""Check saved ScreenDimension through complete native race captures.

This checks integration and capture dimensions, not original GPU raster parity.
Temporary settings and captures never touch installed resources or user saves.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def capture(command, output, width, height):
	run = subprocess.run(command+['--debug-headless',str(output)],capture_output=True,text=True,timeout=90)
	assert run.returncode == 0,(command,run.stdout,run.stderr)
	assert f'Race viewport: {width}x{height}' in run.stdout,run.stdout
	data = output.read_bytes()
	assert data[:2] == b'BM' and struct.unpack_from('<ii',data,18) == (width,height)
	bits = struct.unpack_from('<H',data,28)[0]
	offset = struct.unpack_from('<I',data,10)[0]
	assert bits in (24,32) and len(data) >= offset+((width*bits+31)//32)*4*height
	assert len(set(data[offset:])) > 4,'empty/flat capture'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-race-viewport-') as directory:
		root = Path(directory)
		cfg = root/'test.cfg'
		output = root/'screen.bmp'
		drive = [str(build_executable(args.build, 'openmotor')), '--debug-race','--assets',str(args.gamedir),'--debug-no-records','--debug-mute',
			'--debug-track','1','--debug-car','1','--debug-opponents','2','--debug-ticks','600','--debug-ai','2', '--debug-window-titles', '1']
		cases = [[],['--debug-results'],['--debug-name','Ramlosa','--debug-team','H2O'],
			['--debug-name','tribute to tron'],['--debug-name','r3tr0','--debug-team','a500']]
		count = 0

		for width,height in ((320,200),(640,480),(854,481),(1280,720)):
			saved = (f'ScreenDimension {width} {height}\nInfoDetail All\nNamePlates Transparent\n'
				'SFXVolume 0\nMusicVolume 0\n').encode()
			cfg.write_bytes(saved)

			for extra in cases:
				capture(drive+['--debug-settings',str(cfg)]+extra,output,width,height)
				assert cfg.read_bytes() == saved,'capture changed preferences'
				count += 1

			demo = [str(build_executable(args.build, 'openmotor')), '--debug-demo',str(args.gamedir/'Demos/JeePen.MDE'),
				'--assets',str(args.gamedir),'--debug-settings',str(cfg),'--debug-mute','--debug-ticks','600',
				'--debug-window-titles', '1']

			for extra in ([],['--debug-ghost']):
				capture(demo+extra,output,width,height)
				assert cfg.read_bytes() == saved,'playback capture changed preferences'
				count += 1

			prefix = root/'season'
			run = subprocess.run([str(build_executable(args.build, 'mh-results-profile-assets')),str(args.gamedir),str(prefix),
				str(width),str(height)],capture_output=True,text=True,timeout=90)
			assert run.returncode == 0,(run.stdout,run.stderr)

			for entrants in (2,8):
				data = root.joinpath(f'season-{entrants}.bmp').read_bytes()
				assert struct.unpack_from('<ii',data,18) == (width,height)
				count += 1

			print(f'{width}x{height}: race, results, underwater, TRON, RETRO, Demo, Ghost and season passed',flush=True)

		capture(drive+['--debug-no-settings'],output,960,640)

		for width,height in ((0,480),(319,480),(4097,480),(640,199),(640,2161),(-1,480)):
			saved = f'ScreenDimension {width} {height}\n'.encode()
			cfg.write_bytes(saved)
			run = subprocess.run(drive+['--debug-settings',str(cfg),'--debug-check'],capture_output=True,text=True,timeout=30)
			assert run.returncode != 0 and 'ScreenDimension must be' in run.stderr,(width,height,run)
			assert cfg.read_bytes() == saved

		print(f'{count} selected-size captures, default 960x640 and six invalid dimensions passed')


if __name__ == '__main__':
	main()
