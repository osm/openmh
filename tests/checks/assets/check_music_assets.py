#!/usr/bin/env python3
"""Qualify native CD PCM, completion, repetition and device ownership.

Uses synthetic short/malformed tracks and all ten shipped CD tracks. The
independent PCM comparison tests native resampling, not MCI device timing.
"""

from tests.checks.support.build import build_executable
import argparse
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


from tests.checks.support.process import COMMAND_TIMEOUT


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--music', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-music-assets-') as temporary:
		root = Path(temporary)

		with wave.open(str(root/'track-02.wav'), 'wb') as f:
			f.setparams((2, 2, 44100, 0, 'NONE', ''))
			f.writeframes(struct.pack('<hh', 16384, -8192)*441)

		(root/'track-03.wav').write_bytes(b'RIFF\0\0\0\0WAVE')

		with wave.open(str(root/'track-05.wav'), 'wb') as f:
			f.setparams((2,2,44100,0,'NONE',''))
			f.writeframes(struct.pack('<hh',16384,-8192)*55125)

		output = root/'music.pcm'
		subprocess.run([str(build_executable(args.build, 'mh-audio-clock')), str(args.gamedir), str(root)],
			check=True, timeout=30,
			env=dict(os.environ, SDL_AUDIO_DRIVER='dummy'))
		run = subprocess.run([str(build_executable(args.build, 'mh-music-assets')), str(args.gamedir), str(root),
			str(args.music), str(output), '--debug-audio-trace', '1'], capture_output=True,text=True,
			env=dict(os.environ, SDL_AUDIO_DRIVER='dummy'), timeout=COMMAND_TIMEOUT)
		assert run.returncode == 0,run.stdout+'\n'+run.stderr
		stages = run.stderr.split('crash-lifetime ')[1:]
		assert [s.splitlines()[0] for s in stages] == [*map(str,range(1,10)),'end']

		def crash(stage,event,which=0):
			states = [line.split()[2:] for line in stages[stage-1].splitlines() if line.startswith(event+' ')]
			state = states[which]
			assert len(state) == 50

			return (int(state[30]),int(state[31]),float(state[39]),int(state[32]),int(state[48]),int(state[49]))

		assert crash(1,'audio-results before') == (0,0,.75,326,0,1)
		assert crash(2,'audio-state open') == crash(2,'audio-restart after') == (-1,0,.75,13,1,0)
		assert crash(2,'audio-state close',-1) == (-1,2,.75,13,1,0)
		assert crash(3,'audio-state open') == (-1,2,.75,13,1,0)
		assert crash(3,'audio-results before') == (5,1,.75,13,0,1)
		assert crash(4,'audio-state open') == (-1,1,.75,13,1,0)
		assert crash(4,'audio-restart before') == (0,0,.75,326,0,1)
		assert crash(4,'audio-restart after') == (0,0,.75,326,0,0)
		assert crash(5,'audio-state open') == (-1,0,.75,13,0,0)
		assert crash(5,'audio-results before') == (0,0,.75,326,0,1)
		assert crash(6,'audio-state open') == (-1,0,.75,13,1,0)
		assert crash(6,'audio-state close') == (5,1,.75,13,0,0)
		assert crash(7,'audio-results before') == (5,1,.75,326,0,0)
		before = [s.split()[2:] for s in stages[6].splitlines() if s.startswith('audio-results before ')][0]

		# Four active requests plus the inactive pending crash; the distant
		# ambient source is queued too, until callback eligibility rejects it.
		assert list(map(int,before[40:43])) == [4,5,0],before
		closed = [s.split()[2:] for s in stages[6].splitlines() if s.startswith('audio-state close ')][0]
		assert list(map(int,closed[40:43])) == [0,0,0],closed

		def activity(stage,event,which=0):
			states = [s.split()[2:] for s in stages[stage-1].splitlines() if s.startswith(event+' ')]

			return list(map(int,states[which][40:43]))

		assert activity(8,'audio-restart before') == [4,0,5]
		assert activity(8,'audio-restart after') == [0,0,1]
		assert activity(8,'audio-listener before',-1) == activity(8,'audio-listener after',-1) == [0,0,1]
		assert activity(8,'audio-results after') == [0,0,0]
		assert activity(9,'audio-restart before') == [4,5,0]
		assert activity(9,'audio-restart after') == [0,1,0]
		assert activity(9,'audio-results before') == [0,0,1]
		assert activity(9,'audio-results after') == [0,0,0]
		states = [line.split() for line in run.stderr.splitlines() if line.startswith('audio-results ')]
		assert len(states) % 2 == 0
		dirty = 0

		for before,after in zip(states[::2],states[1::2]):
			assert before[1] == 'before' and after[1] == 'after'
			assert len(before) == len(after) == 52

			if list(map(int,before[42:45])) == [3,1,2]:
				dirty += 1

			assert list(map(int,after[42:45])) == [0,0,0],after

		assert dirty == 14,dirty
		closed = [line.split()[2:] for line in run.stderr.splitlines() if line.startswith('audio-state close ')]
		assert len(closed) >= 14

		for state in closed[-14:]:
			assert list(map(int,state[40:43])) == [0,0,0],state

		samples = struct.unpack('=10240f', output.read_bytes())

		for song in range(2, 12):
			with wave.open(str(args.music/f'track-{song:02}.wav')) as f:
				assert f.getparams()[:3] == (2, 2, 44100)
				f.setpos(f.getnframes()//2)
				source = list(struct.iter_unpack('<hh', f.readframes(472)))

			for frame in range(512):
				index, fraction = divmod(frame*44100, 48000)

				for channel in range(2):
					a, b = source[index][channel], source[index+1][channel]
					expected = (a+(b-a)*fraction/48000)*.35/32768
					actual = samples[(song-2)*1024+frame*2+channel]
					assert math.isfinite(actual) and abs(actual-expected) < 1/32768, (song, frame, channel, actual, expected)

		print('5,120 stereo frames from all ten extracted tracks match independent PCM')
		print('Independent race/frontend playback, focus pause, immediate stop and joined close/reopen pass')
		print('Original CD repeat deadlines, fractional tails, mute/unmute and results suppression pass')
		print('Live independent effects worker consumes car/camera movement without another producer pass')
		print('All seven results modes, with CD enabled/muted, release playing and pending menu effects')
		print('Nine real-asset race loads preserve crash history, restart/listener aliases and complete results teardown')


if __name__ == '__main__':
	main()
