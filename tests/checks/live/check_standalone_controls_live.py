#!/usr/bin/env python3
"""Check saved CLO bindings in each direct X11 debug session.

Run under xvfb-run. CFG/CLO overrides, saves and captures stay in isolated
directories.
"""

from tests.checks.support.build import build_executable
import argparse
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from tests.checks.live.check_legacy_names_live import read_cfg, write_cfg
from tests.checks.live.check_menu_controls_live import snapshot
from tests.checks.support.installation import prepare_view
from tests.checks.support.menu import diagnostic, recordable, ticks, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo
from tests.checks.live.check_view_controls_live import mode

MDE30_VERSION = bytes.fromhex('00004040')
MDE30_PLAYERS_OFFSET = 36
MDE30_CONTROL_OFFSET = 77


def position(log):
	lines = [line.split() for line in log.read_text().splitlines(keepends=True)
		if line.startswith('camera-frame ') and line.endswith('\n')]

	return tuple(map(float, lines[-1][7:10]))


def run(command, env, log, moving, recording=False):
	with log.open('w') as stream:
		process = subprocess.Popen(command, env=env, cwd=log.parent, stdout=stream, stderr=stream)

		try:
			window = window_for(process, log, r' \| ')
			xdo('windowfocus', '--sync', window)
			wait_frames(process, log, frames(log), 6)
			trace = [line.split() for line in log.read_text().splitlines(keepends=True)
				if line.startswith('camera-frame ') and line.endswith('\n')]

			if trace[-1][6] == '1':
				# Xvfb focus suspension persists after refocusing. P belongs
				# to the camera in this profile; resume via its menu binding.
				for button in ('v', 'v'):
					key(window, button, process, log)

			wait(process, log, lambda: ticks(log) > 1800)
			wait_frames(process, log, frames(log), 6)

			# Profile P owns cockpit; it cannot also invoke quick pause.
			for button, wanted in (('p', 1), ('g', 2), ('f', 0)):
				before = ticks(log)
				key(window, button, process, log)
				wait_frames(process, log, frames(log), 2)
				assert mode(log) == wanted and ticks(log) > before, diagnostic(log)

			key(window, 'F1', process, log)
			assert mode(log) == 0, diagnostic(log)

			if moving:
				before = position(log)
				xdo('keydown', 'Up')

				try:
					wait_frames(process, log, frames(log), 12)
				finally:
					xdo('keyup', 'Up')

				assert math.dist(position(log), before) < 1, diagnostic(log)
				xdo('keydown', 'x')

				try:
					wait(process, log, lambda: math.dist(position(log), before) > 5)
				finally:
					xdo('keyup', 'x')

			# The configured menu key works in each session (network remains
			# live), and the original raw Escape shortcut cannot steal it.
			key(window, 'v', process, log)
			key(window, 'v', process, log)
			key(window, 'p', process, log)
			assert mode(log) == 1, diagnostic(log)

			if recording:
				recordable(process, log)

			close_window(window)
			assert process.wait(timeout=30) == 0, diagnostic(log)
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)


def exercise(args, root):
	source = args.gamedir
	installation = root/'Motorhead'
	prepare_view(installation, source)
	args.gamedir = installation
	user = installation/'userdata'
	controls = user
	controls.mkdir(parents=True)
	profile = controls/'Keyboard.clo'
	write_cfg(profile, dict(LayoutName=b'CustomTest', TurnLeft=b'MouseLeft', TurnRight=b'MouseRight',
		Accelerate=b'X', Brake=b'C', InCarView=b'P', OutCarView=b'F', CameraView=b'G',
		GameMenu=b'V', Info=b'I', Horn=b'B', HandBrake=b'K', RearView=b'O'))
	original = snapshot(args.gamedir/'Game')
	saved_profile = profile.read_bytes()
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	recording = root/'Local.MDE'

	for name in ('drive', 'league', 'ghost', 'demo', 'network'):
		cfg = root/(name+'.CFG')
		write_cfg(cfg, dict(ControlName=b'CustomTest', ScreenDimension=b'640 400',
			Renderer=b'Software' if args.renderer == 'software' else b'Glide',
			SFXVolume=b'0', MusicVolume=b'0', MenuConfirmation=b'Off',
			DetailMode=b'Custom', ViewDistance=b'40', CarDetail=b'Low', TrackDetail=b'Low'))
		mode = {'drive': 'race', 'ghost': 'demo'}.get(name, name)
		command = [str(build_executable(args.build, 'openmotor')), '--debug-' + mode]

		if name == 'drive':
			command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--debug-record-file', str(recording), '--debug-no-records', '--debug-no-scenery',
				'--debug-camera-trace', '1',
				'--debug-window-titles', '1']
		elif name == 'league':
			command += ['--debug-save', str(root/'Test.lgf'), '--debug-new', 'Test', '--debug-no-records',
				'--debug-progress', str(root/'Progress')]
		elif name in ('ghost', 'demo'):
			command += [str(args.gamedir/'Demos'/'JeePen.MDE'), '--debug-no-scenery']

			if name == 'ghost':
				command += ['--debug-ghost']
		else:
			command = [str(build_executable(args.build, 'openmotor')), '--debug-network', '--debug-terminal-lobby', '--debug-host', '127.0.0.1',
				'--debug-players', '1', '--debug-port', str(45000+os.getpid()%10000), '--debug-wait', '30',
				'--debug-no-records', '--debug-no-scenery', '--debug-camera-trace', '1', '--debug-window-titles', '1']

		command += ['--assets', str(args.gamedir), '--debug-settings', str(cfg),
			'--debug-controls', str(controls), '--debug-mute',
			'--debug-camera-trace', '1', '--debug-window-titles', '1']
		run(command, env, root/(name+'.log'), name != 'demo', name == 'drive')
		assert f'Race renderer: {"Software" if args.renderer == "software" else "Glide"}' in (root/(name+'.log')).read_text()
		assert read_cfg(cfg)[b'ControlName'] == b'CustomTest'
		assert profile.read_bytes() == saved_profile and snapshot(args.gamedir/'Game') == original
		print(f'{name}: saved controls, held camera bindings, shortcut ownership, menu and CFG/CLO preservation passed', flush=True)

	data = recording.read_bytes()
	assert data[:4] == MDE30_VERSION, 'new take did not use MDE 3.0'

	# MDE 3.0 adds a horn name before colors and device identity.
	control_type = struct.unpack_from('<I', data, MDE30_PLAYERS_OFFSET+MDE30_CONTROL_OFFSET)[0]
	assert control_type == 2, 'local recording lost selected mouse identity'

	# Explicit overrides also work without CFG storage. The selected name
	# defaults to Keyboard, so give this override its matching display name.
	write_cfg(profile, dict(LayoutName=b'Keyboard', InCarView=b'P', OutCarView=b'F', CameraView=b'G', GameMenu=b'V'))
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir), '--debug-no-settings',
		'--renderer', args.renderer,
		'--debug-controls', str(controls), '--debug-no-records', '--debug-no-scenery', '--debug-mute',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	run(command, env, root/'no-settings.log', False)

	# No implicit CFG is created by explicit-only control loading.
	assert not (root/'userdata/motorhead.cfg').exists()
	args.gamedir = source
	check_paths(args, root)
	assert snapshot(args.gamedir/'Game') == original
	print('Explicit controls without CFG, missing/protected directory rejection and recording device metadata passed', flush=True)


def check_paths(args, root):
	original = snapshot(args.gamedir/'Game')
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build, 'openmotor')), '--debug-race', '--assets', str(args.gamedir),
		'--debug-no-settings', '--renderer', args.renderer,
		'--debug-no-records', '--debug-no-scenery', '--debug-mute', '--debug-window-titles', '1']
	missing = subprocess.run(command+['--debug-controls', str(root/'Missing'), '--debug-check'],
		env=env, capture_output=True, text=True, timeout=60)
	assert missing.returncode != 0 and 'writable directory' in missing.stderr, missing
	protected = subprocess.run(command+['--debug-controls', str(args.gamedir/'Game'), '--debug-check'],
		env=env, capture_output=True, text=True, timeout=60)
	assert protected.returncode != 0, protected
	assert snapshot(args.gamedir/'Game') == original
	print('Missing/protected control directory rejection and unchanged assets passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--renderer', choices=('glide', 'software'), default='glide')
	parser.add_argument('--output', type=Path)
	parser.add_argument('--paths-only', action='store_true')
	args = parser.parse_args()
	check = check_paths if args.paths_only else exercise

	if args.output:
		args.output.mkdir(parents=True, exist_ok=True)
		check(args, args.output)
	else:
		with tempfile.TemporaryDirectory(prefix='mh-standalone-controls-') as temporary:
			check(args, Path(temporary))


if __name__ == '__main__':
	main()
