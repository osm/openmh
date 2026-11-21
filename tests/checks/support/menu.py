"""Menu interaction and bounded waits for observable application state."""

import subprocess
import time
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def diagnostic(log):
	lines = log.read_text(errors='replace').splitlines()

	return '\n'.join([line for line in lines if not line.startswith('camera-frame ')][-45:]
		+ [line for line in lines if line.startswith('camera-frame ')][-4:])


def wait(process, log, condition, seconds=60):
	deadline = time.monotonic()+seconds

	while not condition():
		assert process.poll() is None and time.monotonic() < deadline, diagnostic(log)
		time.sleep(.02)


def menu_key(window, name):
	xdo('windowfocus', '--sync', window)
	xdo('keydown', name)

	try:
		time.sleep(.09)
	finally:
		xdo('keyup', name)

	time.sleep(.35)


def menu_text(window, text):
	for char in text:
		menu_key(window, 'space' if char == ' ' else char)


def save_recording(process, log, menu, name):
	wait(process, log, lambda: 'Recording dialog: save' in log.read_text())
	time.sleep(.4)
	entries = log.read_text().count('Recording name entry')
	menu_key(menu, 'Return')
	wait(process, log, lambda: log.read_text().count('Recording name entry') > entries)
	menu_text(menu, name)
	returned = log.read_text().count('Frontend resumed: main')
	menu_key(menu, 'Return')
	wait(process, log, lambda: log.read_text().count('Frontend resumed: main') > returned)


def window_for(process, log, pattern):
	result = []

	def search():
		found = subprocess.run(['xdotool', 'search', '--all', '--onlyvisible', '--pid', str(process.pid),
			'--name', pattern], capture_output=True, text=True, timeout=10)

		if found.returncode == 0 and found.stdout.strip():
			result.append(int(found.stdout.splitlines()[-1]))

		return bool(result)

	wait(process, log, search)

	return result[0]


def ticks(log):
	lines = [line.split() for line in log.read_text(errors='replace').splitlines(keepends=True)
		if line.startswith('camera-frame ') and line.endswith('\n')]

	return int(lines[-1][4]) if lines else 0


def intro_ms(log):
	lines = [line.split() for line in log.read_text(errors='replace').splitlines(keepends=True)
		if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:')
		and line.endswith('\n')]

	return float(lines[-1][16]) if lines else 0


def recordable(process, log):
	# Existing recording begins after six simulated seconds, and samples
	# at most once per update with a strict >62.5 ms interval. Cover both
	# high frame rates and slow ASan/software rendering without wall sleeps.
	# Physics slice counts no longer represent elapsed time.
	wait(process, log, lambda: intro_ms(log) > 6250)

	for _ in range(4):
		wait_frames(process, log, frames(log), 20)

	wait(process, log, lambda: intro_ms(log) > 12500)


def race_and_return(process, menu, log, profile, restart=False):
	previous = log.read_text().count('Frontend resumed: main')
	launches = log.read_text().count(f'Race renderer: {profile}')

	for name in ('Escape', 'Escape', 'Left', 'Left', 'Left', 'Return', 'Return'):
		menu_key(menu, name)

	race = window_for(process, log, r'\| 8 cars \|')
	xdo('windowfocus', '--sync', race)
	wait_frames(process, log, frames(log), 6)
	assert log.read_text().count(f'Race renderer: {profile}') == launches+1, diagnostic(log)

	# A focus transfer under Xvfb can leave the race suspended. Regaining
	# focus deliberately does not resume a human race; open and close its pause menu.
	trace = [line.split() for line in log.read_text().splitlines()
		if line.startswith('camera-frame ') and not line.startswith('camera-frame columns:')]

	if trace[-1][6] == '1':
		key(race, 'Escape', process, log)
		key(race, 'Escape', process, log)

	wait(process, log, lambda: intro_ms(log) > 12500)

	if restart:
		for name in ('Escape', 'Down', 'Return'):
			key(race, name, process, log)

		assert intro_ms(log) < 6000, diagnostic(log)
		wait_frames(process, log, frames(log), 6)
		wait(process, log, lambda: intro_ms(log) > 12500)
		assert log.read_text().count(f'Race renderer: {profile}') == launches+1

	for name in ('Escape', 'Up'):
		key(race, name, process, log)

	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: log.read_text().count('Frontend resumed: main') > previous)
	finally:
		xdo('keyup', 'Return')

	assert window_for(process, log, 'main-menu preview') == menu
