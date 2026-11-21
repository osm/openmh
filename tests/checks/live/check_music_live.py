#!/usr/bin/env python3
"""Exercise frontend/course/abort/menu CD handoffs with real X11 input.

Run under xvfb-run. SDL dummy audio tests device ownership and submission,
not physical speaker output. CFG and user storage are private fixtures.
"""

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
import wave

from tests.checks.support.settings import encode
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def run(args, root, selected, repeat, timed=False):
	cfg = root/'test.cfg'
	log = root/'live.log'
	gamedir = args.gamedir

	if timed:
		music_root = root/'music'
		music_root.mkdir()

		for track in (2,3,4,11):
			with wave.open(str(music_root/f'track-{track:02}.wav'),'wb') as f:
				f.setparams((2,2,44100,0,'NONE',''))
				f.writeframes(struct.pack('<hh',8192,-4096)*55125)

		gamedir = root / 'installation'
		prepare_view(gamedir, args.gamedir, {'Music': music_root})

	def repeats():
		return re.findall(r'Music repeat: track (\d+), start (\d+) -> (\d+), duration (\d+), frame ([\d.]+) -> ([\d.]+)',
			log.read_text())

	cfg.write_bytes(encode(dict(PlayerName='Music driver', TeamName='NOW',
		CarName=fields(args.gamedir/'Game/car01.car')['carname'],
		TrackName=fields(args.gamedir/'Game/Track1.trk')['trackname'],
		GameType='Single', TrackLaps='1', RecordRace='Off', MenuConfirmation='Off',
		ViewDistance='40', SFXVolume='40', MusicVolume='80', MenuCDTrack=str(selected), CDLoop=repeat)))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(gamedir),
		'--debug-settings', str(cfg),
		'--debug-recordings', str(root/'recordings'), '--debug-no-records', '--debug-require-audio', '--debug-page', 'car',
		'--debug-camera-trace', '1',
		'--debug-music-trace', '1',
		'--debug-window-titles', '1']

	with log.open('w') as out:
		process = subprocess.Popen(command, stdout=out, stderr=out, env=env)

		try:
			menu = window_for(process, log, 'main-menu preview')
			time.sleep(.5)
			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)

			if timed:
				if repeat == 'On':
					wait(process,log,lambda: len(repeats()) >= 2)
				else:
					time.sleep(1.5)
					assert not repeats(),diagnostic(log)

			key(race,'Escape',process,log)

			if timed and repeat == 'On':
				count = len(repeats())
				wait(process,log,lambda: len(repeats()) >= count+2)

				for track,start,end,duration,before,after in repeats():
					assert int(track) == 4 and int(duration) == 1000 and float(after) == 0

					# A live frame can arrive after PCM completion.
					assert ((int(end)-int(start))&0xffffffff) != 0

			key(race,'Up',process,log)
			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			assert window_for(process, log, 'main-menu preview') == menu
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			music = [line for line in log.read_text().splitlines() if ' music:' in line]
			assert music == [f'Frontend music: CD track {selected & 255}',
				f'Race music: CD track 4, repeat {int(repeat == "On")}',
				f'Frontend music: CD track {selected & 255}'], music
			assert 'music disabled' not in log.read_text().lower(), diagnostic(log)
			print(f'X11 music handoff: menu {selected & 255} -> course 4 -> abort -> menu; CDLoop {repeat}', flush=True)

			if timed:
				print(f'X11 timed CDLoop {repeat}: race and pause-menu polling, abort return pass',flush=True)
		except Exception:
			print(diagnostic(log), flush=True)

			raise
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
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	for selected, repeat in ((3, 'On'), (267, 'Off')):
		with tempfile.TemporaryDirectory(prefix='mh-music-live-') as directory:
			run(args, Path(directory), selected, repeat)

	for repeat in ('On','Off'):
		with tempfile.TemporaryDirectory(prefix='mh-music-repeat-live-') as directory:
			run(args,Path(directory),3,repeat,True)


if __name__ == '__main__':
	main()
