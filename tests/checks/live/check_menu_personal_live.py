#!/usr/bin/env python3
"""Check Personal edits, CFG persistence and race profiles with real X11 input.

Run under xvfb-run. Uses isolated saves and Swedish keyboard input; installed
assets stay read-only.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from tests.checks.live.check_legacy_names_live import layout, marker, read_cfg, write_cfg
from tests.checks.support.application import frontend
from tests.checks.support.images import bmp_rgb
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, menu_text, recordable, wait, window_for
from tests.checks.support.x11 import frames, key, wait_frames, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def rgb(value):
	return f'{value >> 16} {(value >> 8) & 255} {value & 255}'.encode('ascii')


def pick(colors, value, direction):
	components = lambda n: ((n >> 16) & 255, (n >> 8) & 255, n & 255)
	nearest = min(range(32), key=lambda i: sum((a - b)**2 for a, b in zip(components(colors[i]), components(value))))

	return colors[(nearest + direction) % 32]


def run(args, root):
	cfg, log = root / 'test.cfg', root / 'live.log'
	act = (args.gamedir / 'Data/Default.ACT').read_bytes()
	colors = [int.from_bytes(act[3 * i:3 * i + 3], 'big') for i in range(255, 223, -1)]
	body = [colors[i] for i in (0, 1, 31)]
	player = 0x123456
	base = dict(PlayerName=b'A', TeamName=b'B', PlayerColour=rgb(player),
		CustomCarColours=b'Off', CarColour1=rgb(body[0]), CarColour2=rgb(body[1]), CarColour3=rgb(body[2]),
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'].encode('ascii'),
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'].encode('ascii'),
		GameType=b'TimeAttack', TrackLaps=b'1', RecordRace=b'On', MenuConfirmation=b'Off',
		SFXVolume=b'0', MusicVolume=b'0', DetailMode=b'Custom', ViewDistance=b'40', ScreenDimension=b'640 400')
	base.update({f'ShortKey{i}': b'k' for i in range(10)})
	write_cfg(cfg, base)
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-recordings', str(root / 'Recordings'), '--debug-no-records', '--audio', '0',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']

	with frontend(command, [], log, env) as (process, menu):
		for button in ('Right', 'Right', 'Right', 'Return', 'Return', 'BackSpace'):
			menu_key(menu, button)

		layout('se')

		try:
			for button in ('aring', 'adiaeresis', 'odiaeresis'):
				menu_key(menu, button)
		finally:
			layout('us')

		for button in ('Down', 'BackSpace'):
			menu_key(menu, button)

		menu_text(menu, 'swe')

		for button in ('Down', 'Right'):
			menu_key(menu, button)

		player = pick(colors, player, 1)

		for i in range(10):
			menu_key(menu, 'Down')
			menu_text(menu, str(i))

		for button in ('Down', 'Right'):
			menu_key(menu, button)  # Custom On.

		for i, button in enumerate(('Right', 'Left', 'Right')):
			menu_key(menu, 'Down')
			menu_key(menu, button)
			body[i] = pick(colors, body[i], 1 if button == 'Right' else -1)

		# Escape and Enter both keep changes. Re-entry retains row 16.
		for button in ('Escape', 'Return', 'Left', 'Return', 'Return'):
			menu_key(menu, button)

		body[2] = pick(colors, body[2], -1)

	saved = read_cfg(cfg)
	expected = {b'PlayerName': b'\xe5\xe4\xf6', b'TeamName': b'swe', b'CustomCarColours': b'On',
		b'PlayerColour': rgb(player), **{f'CarColour{i+1}'.encode(): rgb(value) for i, value in enumerate(body)},
		**{f'ShortKey{i}'.encode(): f'k{i}'.encode() for i in range(10)}}
	assert all(saved.get(k) == v for k, v in expected.items()), (saved, expected, diagnostic(log))
	assert marker(log, 'entries 3, focus 16, custom 1') and marker(log, 'page personal'), diagnostic(log)
	print('Personal X11: Main/Options entry, Swedish identity, all ten shortcuts, four colors, '
		'Enter/Escape return, retained focus and active-page save passed', flush=True)

	before = cfg.read_bytes()
	image = root / 'personal.bmp'
	indexed = root / 'personal.idx'
	result = subprocess.run(
		command + ['--debug-page', 'personal', '--debug-selection', '16', '--debug-frames', '120',
			'--debug-capture', str(image), '--debug-indexed', str(indexed)],
		env=env,
		capture_output=True,
		timeout=60,
	)
	assert result.returncode == 0 and b'focus 16, custom 1' in result.stdout, result
	assert cfg.read_bytes() == before
	data = indexed.read_bytes()
	pixels = bmp_rgb(image)
	assert len(data) == 256768 and pixels == b''.join(data[p * 3:p * 3 + 3] for p in data[768:])
	assert data[18:21] == player.to_bytes(3, 'big')
	assert len(set(pixels)) > 32

	for i, value in enumerate(colors):
		assert data[(128 + i) * 3:(129 + i) * 3] == value.to_bytes(3, 'big')

	for i, value in enumerate([player] + body):
		assert data[(160 + i) * 3:(161 + i) * 3] == value.to_bytes(3, 'big')

	if args.capture_dir is not None:
		args.capture_dir.mkdir(parents=True, exist_ok=True)
		(args.capture_dir / 'personal.bmp').write_bytes(image.read_bytes())

	print('Personal reload/capture: unchanged CFG, all choice/widget palette bytes and indexed/RGB output passed', flush=True)

	# Edit after loading, then launch without restarting the frontend. The
	# recording proves the current profile reaches the actual race session.
	with frontend(command, ['--debug-page', 'personal'], log, env) as (process, menu):
		menu_text(menu, 'x')

		for button in ('Escape', 'Escape', 'Return', 'Return'):
			menu_key(menu, button)

		race = window_for(process, log, r'TIME ATTACK \| 1 cars')
		xdo('windowfocus', '--sync', race)
		recordable(process, log)
		assert read_cfg(cfg)[b'PlayerName'] == b'\xe5\xe4\xf6x'

		for button in ('Escape', 'Up'):
			key(race, button, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: marker(log, 'Recording dialog: save'))
		finally:
			xdo('keyup', 'Return')

		time.sleep(.4)
		menu_key(menu, 'Return')
		wait(process, log, lambda: marker(log, 'Recording name entry'))
		menu_text(menu, 'Personal take')
		menu_key(menu, 'Return')
		wait(process, log, lambda: marker(log, 'Frontend resumed: main'))

	take = root / 'Recordings/personal take.mde'
	data = take.read_bytes()

	# New takes use MDE 3.0: each player adds a 16-byte horn before colors.
	# Check disk fields independently of the recording writer.
	assert data[:4] == bytes.fromhex('00004040'), 'new take did not use MDE 3.0'
	local = struct.unpack_from('<I', data, 32)[0]
	offset = 36 + 81 * local
	assert data[offset + 4:offset + 20].split(b'\0')[0] == b'\xe5\xe4\xf6x'
	assert data[offset + 20:offset + 25].split(b'\0')[0] == b'swe'
	assert struct.unpack_from('<4I', data, offset + 57) == tuple([player] + body)
	decoded = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(take)], timeout=COMMAND_TIMEOUT)
	assert b'validated ' in decoded
	saved = read_cfg(cfg)
	expected[b'PlayerName'] += b'x'
	assert all(saved.get(k) == v for k, v in expected.items()), saved
	assert not list(root.rglob('*.tmp.*'))
	print('Personal live race: edited legacy identity and four RGB values reach original-format MDE; '
		'frontend return preserves the settings', flush=True)


def edge_cases(args, root):
	cfg, log = root / 'edge.cfg', root / 'edge.log'
	base = dict(PlayerName=b'A', TeamName=b'B', ShortKey0=b'k',
		CarName=fields(args.gamedir / 'Game/car01.car')['carname'].encode('ascii'),
		TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'].encode('ascii'),
		GameType=b'Single', RecordRace=b'Off', MenuConfirmation=b'Off',
		SFXVolume=b'0', MusicVolume=b'0', DetailMode=b'Custom', ViewDistance=b'40', ScreenDimension=b'640 400')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-recordings', str(root / 'Recordings'), '--debug-no-records', '--audio', '0', '--debug-page', 'personal',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	write_cfg(cfg, base)

	with frontend(command, [], log, env) as (process, menu):
		for button in ('semicolon', 'b', 'Down', 'slash', 'slash', 'c', 'Down', 'Down', 'semicolon', 'slash', 'slash'):
			menu_key(menu, button)

	raw = cfg.read_bytes()
	assert b'PlayerName\tA;b\n' in raw and b'TeamName\tB//c\n' in raw and b'ShortKey0\tk;//\n' in raw, raw
	image = root / 'punctuation.bmp'
	result = subprocess.run(
		command + ['--debug-frames', '2', '--debug-capture', str(image)],
		env=env,
		capture_output=True,
		timeout=60,
	)
	assert result.returncode == 0 and b'Settings: name A, team B,' in result.stdout, result
	assert cfg.read_bytes() == raw
	print('Personal punctuation: name/team/shortcut save literally and reload with the original comment rules', flush=True)

	write_cfg(cfg, base)

	with frontend(command, [], log, env) as (process, menu):
		for button in ('BackSpace', 'Down', 'BackSpace', 'Escape', 'Escape', 'Return', 'Return'):
			menu_key(menu, button)

		race = window_for(process, log, r'\| 8 cars \|')
		xdo('windowfocus', '--sync', race)
		wait_frames(process, log, frames(log), 6)
		assert b'PlayerName\t\n' in cfg.read_bytes() and b'TeamName\t\n' in cfg.read_bytes()

		for button in ('Escape', 'Up'):
			key(race, button, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: marker(log, 'Frontend resumed: main'))
		finally:
			xdo('keyup', 'Return')

	# Menu return reloads CFG. Its reader skips key-only lines, restoring the
	# startup player default; the empty identity above still reached the race.
	assert b'PlayerName\tPlayer\n' in cfg.read_bytes() and b'TeamName\t\n' in cfg.read_bytes()
	print('Personal empty identity: live eight-car race and HUD accept empty text; '
		'abort return reloads the default player name', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--capture-dir', type=Path)
	parser.add_argument('--edges-only', action='store_true')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-personal-live-') as directory:
		if not args.edges_only:
			run(args, Path(directory))

		edge_cases(args, Path(directory))


if __name__ == '__main__':
	main()
