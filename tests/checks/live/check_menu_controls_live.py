#!/usr/bin/env python3
"""Exercise the Controls page and user CLO persistence under X11.

Use xvfb-run. This checks actual keyboard delivery, page transitions, saves and reloads.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.support.application import frontend
from tests.checks.support.images import bmp_rgb
from tests.checks.support.menu import diagnostic, menu_key
from tests.checks.support.x11 import xdo


def snapshot(directory):
	return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir() if p.is_file()}


def bind(menu, button):
	menu_key(menu, 'Return')
	menu_key(menu, button)


def action_row(assets, action=0):
	data = (assets / 'Data/MENU.SPR').read_bytes()
	count = struct.unpack_from('<I', data)[0] & 0x7fffffff
	names = [data[4 + i * 12:12 + i * 12].rstrip(b'\0') for i in range(count)]

	return action + (4 if b'tmspx' in names else 0)


def run(args, root):
	cfg, log, controls = root / 'test.cfg', root / 'live.log', root / 'Controls'
	controls.mkdir()
	write_cfg(cfg, dict(ControlName=b'Keyboard', PlayerName=b'Controls', TeamName=b'TEST',
		SFXVolume=b'0', MusicVolume=b'0', MenuConfirmation=b'Off', DetailMode=b'Custom'))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
		'--debug-controls', str(controls), '--debug-recordings', str(root / 'Recordings'), '--debug-no-records', '--audio', '0',
		'--debug-window-titles', '1']
	source = snapshot(args.gamedir / 'Game')
	first_action = action_row(args.gamedir)

	with frontend(command, [], log, env) as (process, menu):
		for button in ('Right', 'Right', 'Right', 'Return', 'Right', 'Right', 'Return', 'Right'):
			menu_key(menu, button)

		for _ in range(first_action):
			menu_key(menu, 'Down')

		# Entering capture waits for every held key to be released. Q pressed
		# during that wait must not become a binding. Tab is a reserved action.
		# xdotool's F1 keysym can synthesize Alt on the Xvfb keymap; use Tab
		# so this test requests one physical key rather than a modifier chord.
		xdo('keydown', 'Return')
		time.sleep(.15)
		xdo('keydown', 'q')
		time.sleep(.15)
		xdo('keyup', 'Return')
		time.sleep(.15)
		xdo('keyup', 'q')
		time.sleep(.2)
		menu_key(menu, 'Tab')
		menu_key(menu, 'Return')

		# A is already ShiftGearDown: that row receives the previous LeftArrow.
		xdo('keydown', 'a')
		time.sleep(.15)
		xdo('keyup', 'a')
		time.sleep(.2)

	keyboard = controls / 'Keyboard.clo'

	if not keyboard.exists():
		keyboard = next(p for p in controls.iterdir() if p.name.lower() == 'keyboard.clo')

	saved = read_cfg(keyboard)
	assert saved[b'TurnLeft'] == b'A' and saved[b'ShiftGearDown'] == b'LeftArrow', (
		saved,
		diagnostic(log),
	)
	assert saved[b'TurnRight'] == b'RightArrow' and saved[b'InCarView'] == b'F1'
	assert f'column 1, row {first_action}, phase 0, mode 1' in diagnostic(log), diagnostic(log)
	assert len(list(controls.iterdir())) == 1 and read_cfg(cfg)[b'ControlName'] == b'Keyboard'
	print('Controls X11: Main/Options entry, held-release gates, reserved key rejection, duplicate swap and active-page save passed', flush=True)

	# Reload the first edit, change every other editable action, and dirty a
	# second profile. Both files must be saved even when another is selected.
	names = ['TurnRight', 'Accelerate', 'Brake', 'ShiftGearUp', 'ShiftGearDown', 'Horn', 'HandBrake', 'RearView', 'GameMenu']
	values = ['Q', 'W', 'S', 'E', 'D', 'R', 'F', 'G', 'V']

	with frontend(command, ['--debug-page', 'controls', '--debug-selection', str(first_action)], log, env) as (process, menu):
		menu_key(menu, 'Right')

		for value in values:
			menu_key(menu, 'Down')
			bind(menu, value.lower())

		# Direct --debug-page startup retains the Options default focus (Personal).
		for button in ('Escape', 'Right', 'Right', 'Return'):
			menu_key(menu, button)  # Retain action row 9.

		for button in ('Left', 'Down', 'Right'):
			menu_key(menu, button)  # Mouse profile.

		for _ in range(9):
			menu_key(menu, 'Up')

		bind(menu, 'j')

		for button in ('Left', 'Up', 'Return', 'Return', 'Down', 'Return'):
			menu_key(menu, button)

	saved = read_cfg(keyboard)
	assert saved[b'TurnLeft'] == b'A' and all(saved[k.encode()] == v.encode() for k, v in zip(names, values)), saved
	mouse = next((p for p in controls.iterdir() if p.name.lower() == 'mouse.clo'), None)
	assert mouse is not None, diagnostic(log)
	assert read_cfg(mouse)[b'TurnLeft'] == b'J'
	assert read_cfg(cfg)[b'ControlName'] == b'Mouse', (read_cfg(cfg), diagnostic(log))
	assert 'entries 3' in diagnostic(log) and f'column 0, row {first_action}, phase 0, mode 1' in diagnostic(log)
	assert 'name Mouse, page options' in diagnostic(log), diagnostic(log)
	assert len(list(controls.iterdir())) == 2
	print('Controls X11: all ten actions, two dirty profiles, retained page state and selected-name CFG persistence passed', flush=True)

	before_cfg, before_profiles = cfg.read_bytes(), snapshot(controls)
	image, indexed = root / 'controls.bmp', root / 'controls.idx'
	result = subprocess.run(
		command + ['--debug-page', 'controls', '--debug-selection', str(first_action + 9), '--debug-frames', '120',
			'--debug-capture', str(image), '--debug-indexed', str(indexed)],
		env=env,
		capture_output=True,
		timeout=60,
	)
	assert result.returncode == 0 and b'name Mouse, page controls' in result.stdout, result
	assert cfg.read_bytes() == before_cfg and snapshot(controls) == before_profiles
	data = indexed.read_bytes()
	pixels = bmp_rgb(image)
	assert len(data) == 256768 and pixels == b''.join(data[p * 3:p * 3 + 3] for p in data[768:])
	assert len(set(pixels)) > 32

	if args.capture_dir is not None:
		args.capture_dir.mkdir(parents=True, exist_ok=True)
		(args.capture_dir / 'controls.bmp').write_bytes(image.read_bytes())

	print('Controls capture: selected profile reload, unchanged CFG/CLO files and indexed/RGB composition passed', flush=True)

	# No-settings without an explicit override directory keeps edits in this
	# process only; neither assets nor existing user overrides may change.
	transient = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-no-settings',
		'--debug-page', 'controls', '--debug-selection', str(action_row(args.gamedir)), '--debug-no-records', '--audio', '0', '--debug-recordings', str(root / 'Recordings'),
		'--debug-window-titles', '1']

	with frontend(transient, [], log, env) as (process, menu):
		menu_key(menu, 'Right')
		bind(menu, 'x')

	assert cfg.read_bytes() == before_cfg and snapshot(controls) == before_profiles
	assert not list((root / 'userdata').glob('*.clo'))
	assert snapshot(args.gamedir / 'Game') == source
	print('Controls session-only edits and original asset preservation passed', flush=True)


def paths(args, root):
	cfg, log = root / 'paths.cfg', root / 'paths.log'
	write_cfg(cfg, dict(ControlName=b'Keyboard', SFXVolume=b'0', MusicVolume=b'0'))
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	common = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-page', 'controls', '--debug-selection', str(action_row(args.gamedir)),
		'--debug-no-records', '--audio', '0', '--debug-recordings', str(root / 'Recordings'), '--debug-window-titles', '1']
	command = common + ['--debug-settings', str(cfg)]
	source = snapshot(args.gamedir / 'Game')

	with frontend(command, [], log, env) as (process, menu):
		menu_key(menu, 'Right')
		bind(menu, 'x')

	directory = root / 'userdata'
	saved = list(directory.glob('*.clo'))
	assert len(saved) == 1 and saved[0].name.lower() == 'keyboard.clo', saved
	assert read_cfg(saved[0])[b'TurnLeft'] == b'X'
	before_cfg, before_profiles = cfg.read_bytes(), snapshot(directory)

	# Default user files must not affect deterministic captures; explicitly
	# selecting their directory must load them and change the visible key.
	images = []

	for explicit in (False, True):
		image = root / f'paths-{explicit}.bmp'
		extra = ['--debug-controls', str(directory)] if explicit else []
		result = subprocess.run(
			command + extra + ['--debug-capture', str(image), '--debug-frames', '120'],
			env=env,
			capture_output=True,
			timeout=60,
		)
		assert result.returncode == 0, result
		images.append(bmp_rgb(image))

	assert images[0] != images[1]
	assert cfg.read_bytes() == before_cfg and snapshot(directory) == before_profiles

	# Explicit control storage remains usable with CFG persistence disabled.
	with frontend(common + ['--debug-no-settings', '--debug-controls', str(directory)], [], log, env) as (process, menu):
		menu_key(menu, 'Right')
		bind(menu, 'z')

	assert read_cfg(saved[0])[b'TurnLeft'] == b'Z' and cfg.read_bytes() == before_cfg
	assert snapshot(args.gamedir / 'Game') == source
	print('Controls paths: default user-data save, explicit capture opt-in and --debug-no-settings/--debug-controls independence passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--capture-dir', type=Path)
	parser.add_argument('--paths-only', action='store_true')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-controls-live-') as directory:
		if not args.paths_only:
			run(args, Path(directory))

		paths(args, Path(directory))


if __name__ == '__main__':
	main()
