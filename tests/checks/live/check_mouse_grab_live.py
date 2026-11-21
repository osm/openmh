#!/usr/bin/env python3
"""Check saved mouse capture, fullscreen transitions and races under X11.

Run on an isolated display with a window manager, as for fullscreen_live.
The X11 probe briefly acquires an otherwise free pointer, then releases it.
"""

import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import tempfile
import time

from tests.checks.support.build import build_executable
from tests.checks.support.installation import prepare_view
from tests.checks.support.application import frontend
from tests.checks.support.menu import menu_key, wait, window_for
from tests.checks.support.settings import read_cfg
from tests.checks.support.x11 import frames, key, wait_frames, xdo


class PointerProbe:
	def __init__(self):
		self.xlib = ctypes.CDLL(ctypes.util.find_library('X11'))
		self.xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
		self.xlib.XOpenDisplay.restype = ctypes.c_void_p
		self.xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
		self.xlib.XDefaultRootWindow.restype = ctypes.c_ulong
		self.xlib.XGrabPointer.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
			ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_int,
			ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
		self.xlib.XGrabPointer.restype = ctypes.c_int
		self.xlib.XUngrabPointer.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
		self.xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
		self.xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
		self.display = self.xlib.XOpenDisplay(None)
		assert self.display, 'cannot open X11 pointer probe'
		self.root = self.xlib.XDefaultRootWindow(self.display)

	def grabbed(self):
		result = self.xlib.XGrabPointer(self.display, self.root, 0, 0,
			1, 1, 0, 0, 0)

		if result == 0:  # GrabSuccess; no other client owns the pointer.
			self.xlib.XUngrabPointer(self.display, 0)
			self.xlib.XSync(self.display, 0)

			return False

		assert result == 1, f'unexpected XGrabPointer result: {result}'

		return True  # AlreadyGrabbed by the game.

	def close(self):
		self.xlib.XCloseDisplay(self.display)


def expect_grab(probe, process, log, window, enabled):
	xdo('windowfocus', '--sync', window)
	wait(process, log, lambda: probe.grabbed() == enabled, seconds=10)


def escape_window():
	# Mouse motion also navigates original menus. Check escape only after
	# keyboard edits, so the test's deliberate motion cannot change the row.
	xdo('mousemove', '--sync', '0', '0')
	time.sleep(.3)
	position = dict(line.split('=', 1) for line in
		xdo('getmouselocation', '--shell').splitlines())
	assert (position['X'], position['Y']) == ('0', '0'), position


def check_race(command, env, log, probe, renderer):
	with frontend(command, ['--renderer', renderer, '--debug-page', 'car'],
		log, env) as (process, menu):
		expect_grab(probe, process, log, menu, False)
		menu_key(menu, 'Return')
		race = window_for(process, log, r'\| 8 cars \|')
		wait_frames(process, log, frames(log), 6)
		expect_grab(probe, process, log, race, False)
		escape_window()

		for name in ('Escape', 'Up'):
			key(race, name, process, log)

		xdo('keydown', 'Return')

		try:
			wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
		finally:
			xdo('keyup', 'Return')

		expect_grab(probe, process, log, menu, False)

	print(f'{renderer}: free pointer in menu, race, and returned menu passed', flush=True)


def run(args, root, probe):
	game = root / 'installation'
	prepare_view(game, args.gamedir.resolve())
	cfg, log = root / 'settings.cfg', root / 'live.log'
	cfg.write_text('Fullscreen Off\nGrabMouse On\nScreenDimension 640 400\n'
		'SFXVolume 0\nMusicVolume 0\nMenuConfirmation Off\n'
		'RecordRace Off\nDetailMode High\n')
	command = [str(build_executable(args.build.resolve(), 'openmotor')), '--user-data', str(root / 'userdata'),
		'--assets', str(game), '--debug-settings', str(cfg), '--debug-no-intro', '--debug-skip-loading-wait', '1',
		'--debug-no-records', '--audio', '0', '--debug-car', '1', '--debug-track', '1',
		'--debug-camera-trace', '1',
		'--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with frontend(command, ['--grab-mouse', '0', '--debug-page', 'graphics',
		'--debug-selection', '27'], log, env) as (process, menu):
		expect_grab(probe, process, log, menu, False)
		menu_key(menu, 'Right')
		expect_grab(probe, process, log, menu, True)
		menu_key(menu, 'Left')
		expect_grab(probe, process, log, menu, False)
		escape_window()

	assert read_cfg(cfg)['GrabMouse'] == 'Off'
	assert read_cfg(cfg)['Fullscreen'] == 'Off'
	print('Free pointer, fullscreen grab/release and saved preference passed', flush=True)

	# No grab flag: these launches must reload the saved Off preference.
	for renderer in ('software', 'glide', 'direct3d', 'powervr'):
		check_race(command, env, log, probe, renderer)

	with frontend(command, ['--grab-mouse', '1'], log, env) as (process, menu):
		expect_grab(probe, process, log, menu, True)

	assert read_cfg(cfg)['GrabMouse'] == 'On'

	with frontend(command, [], log, env) as (process, menu):
		expect_grab(probe, process, log, menu, True)

	print('Explicit grab override and saved On reload passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path,
		required=True)
	args = parser.parse_args()
	probe = PointerProbe()

	try:
		with tempfile.TemporaryDirectory(prefix='mh-mouse-grab-') as directory:
			run(args, Path(directory), probe)
	finally:
		probe.close()


if __name__ == '__main__':
	main()
