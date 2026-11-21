#!/usr/bin/env python3
"""Check frontend CFG restoration, unlock identity and persistent menu edits.

Exercise the CFG parser/writer, cheat recognition and catalog producers through
an unmodified European retail installation, filesystem and menus, including
source-index/name round trips.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.x11 import close_window
from tests.checks.support.images import bmp_rgb
from tests.checks.support.catalog import fields, CAR_ORDER


from tests.checks.support.settings import encode, read_cfg


def live(command, root, values, cars, tracks, horns, env):
	def drive(path, arguments, keys, normal_exit=False):
		log = root/'live.log'

		with log.open('wb') as out:
			process = subprocess.Popen(command+arguments+['--debug-settings', str(path)],
				env=dict(env, SDL_VIDEO_DRIVER='x11'), stdout=out, stderr=subprocess.STDOUT)

			try:
				window = subprocess.check_output(['xdotool', 'search', '--sync', '--pid', str(process.pid),
					'--name', 'Motorhead main-menu preview'], timeout=30).splitlines()[-1].decode()
				subprocess.run(['xdotool', 'windowfocus', '--sync', window], check=True, timeout=10)
				time.sleep(.6)

				for key in keys:
					assert process.poll() is None, log.read_text()
					subprocess.run(['xdotool', 'keydown', key], check=True, timeout=10)

					try:
						time.sleep(.04)
					finally:
						subprocess.run(['xdotool', 'keyup', key], check=True, timeout=10)

					time.sleep(.4)

				if not normal_exit:
					close_window(window)

				assert process.wait(timeout=10) == 0, log.read_text()

				if normal_exit:
					assert 'Exit page completed' in log.read_text(), log.read_text()
			finally:
				if process.poll() is None:
					process.terminate()
					process.wait(timeout=10)

	base = dict(values, CarName=cars[2], TrackName=tracks[8], GameType='Single',
		TrackLaps='5', CatchUp='Off', CarTransmission='Auto', RecordRace='Off', MenuConfirmation='On')
	path = root/'edited.cfg'
	path.write_bytes(encode(base))
	keys = ['Right', 'Down', 'Right', 'Down', 'Right', 'Return', 'Right', 'Right',
		'Down', 'Left', 'Down', 'Right', 'Down', 'Right', 'Escape', 'Escape',
		'Escape', 'Escape', 'Left', 'Return']
	drive(path, ['--debug-page', 'setup'], keys, normal_exit=True)
	saved = read_cfg(path)
	next_horn = horns[horns.index('Honker1')+1]
	wanted = dict(CarName=cars[8], TrackName=tracks[1], TrackLaps='10', CatchUp='On',
		CarTransmission='Manual', RecordRace='On', CarHorn=next_horn, PlayerName=base['PlayerName'],
		TeamName=base['TeamName'], ControlName='Keyboard', Difficulty='Hard', SFXVolume='53.73')
	assert all(saved.get(key) == value for key, value in wanted.items()), (saved, wanted)

	for page, text in (('car', f'car 8, index 2, automatic 0, record 1, horn {next_horn}'),
		('setup', 'track 1, laps 10, catch-up 1')):
		before = path.read_bytes()
		result = subprocess.run(command+['--debug-settings', str(path), '--debug-page', page, '--debug-frames', '2',
			'--debug-capture', str(root/'reloaded.bmp')], env=env, capture_output=True, text=True, timeout=60)
		assert result.returncode == 0 and text in result.stdout, result
		assert path.read_bytes() == before

	# Closing the window while editing a page also keeps its current fields.
	path.write_bytes(encode(base))
	drive(path, ['--debug-page', 'car'], ['Right', 'Down', 'Left'])
	saved = read_cfg(path)
	assert saved['CarName'] == cars[7] and saved['CarTransmission'] == 'Manual', saved
	assert saved['GameType'] == 'Single' and saved['TrackName'] == tracks[8], saved
	assert not list(root.glob('*.tmp.*'))
	print('X11 CFG save/reload passed: track/car/horn edits, original exit flow and active-page window close')


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--live', action='store_true')
	parser.add_argument('--report', type=Path)
	parser.add_argument('--compare', type=Path)
	args = parser.parse_args()
	cars = {i: fields(args.gamedir/'Game'/f'car{i:02}.car')['carname'] for i in range(1, 11)}
	tracks = {i: fields(args.gamedir/'Game'/f'Track{i}.trk')['trackname'] for i in range(1, 9)}
	horns = [next(line.split('//')[0].split(None, 1)[1].strip() for line in p.read_text().splitlines()
		if line.split() and line.split()[0].lower() == 'name')
		for p in sorted((args.gamedir/'Sounds/CarHorns').glob('*.chf'), key=lambda p: p.name.lower())]
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--audio', '0', '--debug-special-cars', '1',
		'--debug-window-titles', '1', '--grab-mouse', '0']
	values = dict(PlayerName='Driver', TeamName='TBL', CarName=cars[4], TrackName=tracks[5],
		GameType='TimeAttack', TrackLaps='10', CatchUp='On', CarTransmission='Manual',
		RecordRace='On', CarHorn='Honker1', Difficulty='Hard', SFXVolume='54',
		MenuConfirmation='NoExit', ControlName='Keyboard')
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-menu-settings-') as temporary:
		root = Path(temporary)
		path = root/'MotorHead.CFG'
		env = dict(os.environ)
		command += ['--user-data', str(root / 'user')]

		def capture(cfg, page, extra=(), expected=(), failure=None):
			path.write_bytes(encode(cfg))
			before = path.read_bytes()
			stamp = path.stat().st_mtime_ns
			raw, bmp = root/'frame.idx', root/'frame.bmp'
			output = subprocess.run(command+['--debug-settings', str(path), '--debug-page', page, '--debug-frames', '120',
				'--debug-capture', str(bmp), '--debug-indexed', str(raw)]+list(extra), env=env,
				capture_output=True, text=True, timeout=60)
			assert path.read_bytes() == before and path.stat().st_mtime_ns == stamp
			assert not (root/'user/motorhead.cfg').exists(), 'capture saved implicit settings'

			if failure:
				assert output.returncode != 0 and failure in output.stderr, output

				return None

			assert output.returncode == 0, output

			for text in expected:
				assert text in output.stdout, (text, output.stdout)

			pixels = raw.read_bytes()
			assert len(pixels) == 256768
			rgb = bmp_rgb(bmp)
			assert rgb == b''.join(pixels[i*3:i*3+3] for i in pixels[768:])
			result = dict(page=page, rgb_sha256=hashlib.sha256(rgb).hexdigest(), state=output.stdout.strip())
			results.append(result)

			return result

		diagnostic = ['--debug-unlocked-cars', '10', '--debug-unlocked-tracks', '8']

		for number, name in cars.items():
			capture(dict(values, CarName=name.swapcase()), 'car', diagnostic,
				[f'car {number}, index {CAR_ORDER.index(number)}, automatic 0, record 1, horn Honker1',
					'Settings: name Driver, team TBL, difficulty 2, effects 137, confirmation 2'])

		for number, name in tracks.items():
			capture(dict(values, TrackName=name.swapcase()), 'setup', diagnostic,
				[f'track {number}, laps 10, catch-up 1, unlocked 8'])

		capture(values, 'car', expected=['Catalog: availability 00, tracks 2/6, cars 3/9', 'car 8, index 2'])
		capture(values, 'setup', expected=['track 8, laps 10, catch-up 1, unlocked 2'])
		cheat = dict(values, PlayerName='r PeTtErSsOn', TeamName='sWe')

		for page in ('car', 'setup'):
			capture(cheat, page, expected=['Unlock cheat active', 'Catalog: availability ff, tracks 8/8, cars 10/10',
				'car 4, index 9' if page == 'car' else 'track 5, laps 10'])

		for cfg, extra in ((values, ['--debug-name', 'R Pettersson', '--debug-team', 'SWE']),
			(dict(cheat, PlayerName='R PetterssonX'), []),
			(dict(cheat, TeamName='SWEx'), []),
			(cheat, ['--debug-name', 'Driver']), (cheat, ['--debug-team', 'ABC'])):
			enabled = bool(extra) and cfg is values
			capture(cfg, 'car', extra, ['Catalog: availability '+('ff, tracks 8/8, cars 10/10' if enabled
				else '00, tracks 2/6, cars 3/9')])

		extra = diagnostic+['--debug-car', '1', '--debug-track', '7', '--debug-mode', '0', '--debug-laps', '3',
			'--debug-catch-up', '0', '--debug-automatic', '1', '--debug-record', '0',
			'--debug-confirmation', '0', '--debug-horn', horns[0]]
		capture(values, 'car', extra, [f'car 1, index 6, automatic 1, record 0, horn {horns[0]}', 'confirmation 0'])
		capture(values, 'setup', extra, ['track 7, laps 3, catch-up 0'])
		capture(values, 'single', extra, ['Single player: selection 0, mode 0, car 1'])

		# Unknown saved names leave the initial source selections unchanged.
		capture(dict(values, CarName='absent', TrackName='absent'), 'car', diagnostic+['--debug-car', '6'],
			['car 6, index 7'])

		# Unknown horn fallback reseeds the shared menu RNG before drawing.
		seed = (int.from_bytes(b'Abse', 'little')*1103515245+12345)&0xffffffff
		selected_horn = horns[((seed >> 16)&32767)%len(horns)]
		unknown = capture(dict(values, CarHorn='AbsentHorn'), 'car', diagnostic, [f'horn {selected_horn}'])
		known = capture(dict(values, CarHorn=selected_horn), 'car', diagnostic+['--debug-seed', str(seed)])
		assert unknown == known, 'CFG horn RNG was not propagated into the frontend'
		capture(dict(values, CarHorn='bad'), 'car', failure='horn name is too short')
		capture(values, 'car', ['--debug-no-settings'], failure='conflicts')
		capture(values, 'car', ['--debug-team', 'ABCDE'], failure='four printable')
		capture(values, 'car', ['--debug-name', 'x'*16], failure='invalid settings initialization')

		# Exercise the selected user-data directory with SDL's offscreen backend.
		# A frame-limited interactive run saves, while captures and explicit
		# --debug-no-settings must ignore even a malformed implicit CFG.
		user = root/'user/motorhead.cfg'
		output = subprocess.run(command+['--debug-frames', '2'], env=dict(env, SDL_VIDEO_DRIVER='offscreen'),
			capture_output=True, text=True, timeout=60)
		assert output.returncode == 0 and user.exists(), output
		saved = read_cfg(user)
		assert saved['CarName'] == cars[8] and saved['TrackName'] == tracks[1], saved
		assert saved['CarHorn'] == horns[0], saved
		user.write_bytes(b'CarHorn bad\n')

		for extra in (['--debug-frames', '2', '--debug-no-settings'],
			['--debug-capture', str(root/'implicit.bmp'), '--debug-frames', '2']):
			output = subprocess.run(command+extra, env=dict(env, SDL_VIDEO_DRIVER='offscreen'),
				capture_output=True, text=True, timeout=60)
			assert output.returncode == 0, output
			assert 'Settings: name Player,' in output.stdout, output
			assert user.read_bytes() == b'CarHorn bad\n'

		if args.live:
			live(command, root, values, cars, tracks, horns, env)

	if args.compare:
		assert results == json.loads(args.compare.read_text()), 'cross-build settings presentation mismatch'

	if args.report:
		args.report.write_text(json.dumps(results, indent=2)+'\n')

	print(f'{len(results)} CFG captures passed: all saved car/track names, unlock identity, overrides, horn RNG and read-only preservation')


if __name__ == '__main__':
	main()
