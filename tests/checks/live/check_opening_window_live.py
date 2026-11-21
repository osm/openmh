#!/usr/bin/env python3
"""Verify that intros and the menu keep one mapped fullscreen X11 window.

Run on an isolated display with a window manager. --natural also watches the
complete retail intro sequence, without skipping any clips.
"""

import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.build import build_executable
from tests.checks.live.check_fullscreen_live import dimensions, stop
from tests.checks.support.installation import prepare_view
from tests.checks.support.menu import diagnostic, menu_key, window_for
from tests.checks.support.x11 import close_window, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


class WindowWatch:
	def __init__(self, window):
		self.xlib = ctypes.CDLL(ctypes.util.find_library('X11'))
		self.xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
		self.xlib.XOpenDisplay.restype = ctypes.c_void_p
		self.xlib.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
			ctypes.c_long]
		self.xlib.XPending.argtypes = [ctypes.c_void_p]
		self.xlib.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
		self.xlib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
		self.xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
		self.display = self.xlib.XOpenDisplay(None)
		assert self.display
		self.xlib.XSelectInput(self.display, window, 1 << 17)  # StructureNotify
		self.xlib.XSync(self.display, 0)

	def check(self):
		event = (ctypes.c_long * 24)()

		while self.xlib.XPending(self.display):
			self.xlib.XNextEvent(self.display, event)
			kind = ctypes.cast(event, ctypes.POINTER(ctypes.c_int))[0]
			assert kind not in (17, 18), f'client window destroyed/unmapped: {kind}'

	def close(self):
		self.xlib.XCloseDisplay(self.display)


def run(args, root, fullscreen, skip):
	log = root / 'live.log'
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	command = [str(build_executable(args.build.resolve(), 'openmotor')), '--user-data', str(root / 'userdata'),
		'--assets', str(root / 'installation'), '--debug-no-settings',
		'--debug-no-records', '--audio', '0',
		'--fullscreen', '1' if fullscreen else '0', '--debug-window-titles', '1']

	with log.open('w') as stream:
		process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)
		watch = None

		try:
			movie = window_for(process, log, 'Motorhead movie')
			time.sleep(1)  # Let the first canvas and window manager settle.
			movie = window_for(process, log, 'Motorhead movie')
			watch = WindowWatch(movie)

			if skip:
				menu_key(movie, 'Return')

			deadline = time.monotonic() + 240
			clips = 0

			while 'main-menu preview' not in xdo('getwindowname', movie):
				watch.check()
				assert process.poll() is None and time.monotonic() < deadline, diagnostic(log)
				current = log.read_text().count('Opening movie:')

				if current != clips:
					clips = current
					print(f'Opening window retained through clip {clips}', flush=True)

				time.sleep(.02)

			watch.check()
			assert window_for(process, log, 'main-menu preview') == movie

			if fullscreen:
				assert dimensions(movie) == tuple(map(int, xdo('getdisplaygeometry').split()))
				assert '_NET_WM_STATE_FULLSCREEN' in subprocess.check_output(
					['xprop', '-id', str(movie), '_NET_WM_STATE'], text=True, timeout=COMMAND_TIMEOUT)

			if not skip:
				assert log.read_text().count('Opening movie:') == 3, diagnostic(log)

			watch.close()
			watch = None
			close_window(movie)
			assert process.wait(timeout=30) == 0, diagnostic(log)
			print(f'Opening -> menu: fullscreen={fullscreen}, skip={skip}, same mapped window passed', flush=True)
		finally:
			if watch is not None:
				watch.close()

			stop(process)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path,
		required=True)
	parser.add_argument('--natural', action='store_true')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-opening-window-') as directory:
		root = Path(directory)
		prepare_view(root / 'installation', args.gamedir.resolve())
		run(args, root, True, True)
		run(args, root, False, True)

		if args.natural:
			run(args, root, True, False)


if __name__ == '__main__':
	main()
