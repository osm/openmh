#!/usr/bin/env python3
"""Exercise the original save/name/overwrite flow using real X11 input.

All races, settings and recordings use private fixtures. Original assets are
read-only. Run with xvfb-run; --build selects GCC or sanitized Clang binaries.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, menu_text, recordable, wait, window_for
from tests.checks.support.settings import encode
from tests.checks.support.x11 import close_window, key, xdo


from tests.checks.support.process import COMMAND_TIMEOUT


def count(log, marker):
	return log.read_text(errors='replace').count(marker)


def decision(process, log, menu, button, marker):
	previous = count(log, marker)
	menu_key(menu, button)
	wait(process, log, lambda: count(log, marker) > previous)
	time.sleep(.25)


def enter_name(process, log, menu, button='Return'):
	decision(process, log, menu, button, 'Recording name entry')


def finish_race(process, log, menu, first=False):
	time.sleep(.6)

	for button in (('Return',) if first else ('Return', 'Return')):
		menu_key(menu, button)

	race = window_for(process, log, r'TIME ATTACK \| 1 cars')
	xdo('windowfocus', '--sync', race)
	recordable(process, log)

	for button in ('Escape', 'Up'):
		key(race, button, process, log)

	previous = count(log, 'Recording dialog: save')
	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: count(log, 'Recording dialog: save') > previous)
	finally:
		xdo('keyup', 'Return')

	assert window_for(process, log, 'main-menu preview') == menu
	time.sleep(.4)


def decoded(args, path):
	data = path.read_bytes()
	assert data[:4] == bytes.fromhex('00004040'), 'new take did not use MDE 3.0'
	assert data[77:93].split(b'\0', 1)[0] == b'Honker2', 'selected horn was not recorded'
	text = subprocess.check_output([str(build_executable(args.build, 'mh-demo-info')), str(path)], text=True, timeout=COMMAND_TIMEOUT)
	assert 'validated ' in text and '"Save driver"' in text, text[:1000]


def capture(args, menu, name):
	if args.capture_dir is not None:
		args.capture_dir.mkdir(parents=True, exist_ok=True)
		subprocess.run(
			['xwd', '-id', str(menu), '-silent', '-out', str(args.capture_dir / (name + '.xwd'))],
			check=True,
			timeout=COMMAND_TIMEOUT)


def run(args):
	with tempfile.TemporaryDirectory(prefix='mh-recording-save-live-') as temporary:
		root = Path(temporary)
		cfg = root / 'private.cfg'
		log = root / 'live.log'
		recordings = root / 'recordings'
		recordings.mkdir()
		cfg.write_bytes(encode(dict(PlayerName='Save driver', TeamName='TEST',
			CarName=fields(args.gamedir / 'Game/car01.car')['carname'], CarHorn='Honker2',
			TrackName=fields(args.gamedir / 'Game/Track1.trk')['trackname'], GameType='TimeAttack',
			RecordRace='On', MenuConfirmation='Off', ViewDistance='40', SFXVolume='0', MusicVolume='0')))
		env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
		command = [str(build_executable(args.build, 'openmotor')), '--user-data', str(root / 'userdata'), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(args.gamedir), '--debug-settings', str(cfg),
			'--debug-recordings', str(recordings), '--debug-no-records', '--audio', '0', '--debug-page', 'car',
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1']

		with log.open('w') as stream:
			process = subprocess.Popen(command, stdout=stream, stderr=stream, env=env)

			try:
				menu = window_for(process, log, 'main-menu preview')
				finish_race(process, log, menu, True)
				capture(args, menu, 'save-question')
				assert not list(recordings.glob('*.mde')) and len(list(recordings.glob('*.tmp.*'))) == 1

				# Initial Cancel discards the private take, without a filename.
				menu_key(menu, 'Right')
				decision(process, log, menu, 'Return', 'Frontend resumed: main')
				assert not list(recordings.iterdir())

				finish_race(process, log, menu)

				# Original Escape on the save question means Save to disc.
				enter_name(process, log, menu, 'Escape')
				decision(process, log, menu, 'Return', 'Recording dialog: save')  # empty name
				enter_name(process, log, menu)
				menu_text(menu, 'Retry')
				menu_key(menu, 'slash')
				capture(args, menu, 'name-entry')
				decision(process, log, menu, 'Return', 'Recording save failed:')
				capture(args, menu, 'save-error')
				assert not list(recordings.glob('*.mde')) and len(list(recordings.glob('*.tmp.*'))) == 1
				menu_key(menu, 'Return')  # native error notice
				menu_key(menu, 'BackSpace')  # remove the forbidden slash
				menu_text(menu, ' 9')
				menu_key(menu, 'BackSpace')
				menu_text(menu, 'one')

				# Escape from the editor reopens the question and retains edits.
				decision(process, log, menu, 'Escape', 'Recording dialog: save')
				enter_name(process, log, menu)
				existing = recordings / 'rETRY ONE.mde'
				existing.write_bytes(b'keep the previous recording')
				decision(process, log, menu, 'Return', 'Recording dialog: overwrite')
				capture(args, menu, 'overwrite-question')
				assert existing.read_bytes() == b'keep the previous recording'
				menu_key(menu, 'Right')
				enter_name(process, log, menu)  # Cancel replacement; return to the unchanged name.
				menu_text(menu, '2')
				decision(process, log, menu, 'Return', 'Frontend resumed: main')
				take = recordings / 'retry one2.mde'
				decoded(args, take)
				assert existing.read_bytes() == b'keep the previous recording'
				original = take.read_bytes()

				finish_race(process, log, menu)
				enter_name(process, log, menu)

				# The name buffer survives whole races. Confirm this by pressing
				# Enter without retyping; Escape confirms original Overwrite.
				decision(process, log, menu, 'Return', 'Recording dialog: overwrite')
				assert take.read_bytes() == original
				decision(process, log, menu, 'Escape', 'Frontend resumed: main')
				decoded(args, take)
				assert not list(recordings.glob('*.tmp.*'))

				finish_race(process, log, menu)
				enter_name(process, log, menu)

				# A replaced file during the overwrite question needs a fresh
				# probe, and the private take must survive the rejection.
				decision(process, log, menu, 'Return', 'Recording dialog: overwrite')
				held = recordings / 'held'
				take.rename(held)
				take.write_bytes(b'new owner')
				decision(process, log, menu, 'Return', 'Recording save failed:')
				assert take.read_bytes() == b'new owner' and held.read_bytes() != b'new owner'
				menu_key(menu, 'Return')
				menu_text(menu, '3')
				decision(process, log, menu, 'Return', 'Frontend resumed: main')
				decoded(args, recordings / 'retry one23.mde')
				assert take.read_bytes() == b'new owner'

				finish_race(process, log, menu)
				enter_name(process, log, menu)

				for _ in 'Retry one23':
					menu_key(menu, 'BackSpace')

				# SDL supplies the active keyboard layout; retain the original
				# eight-bit glyph/name bytes instead of UTF-8 byte fragments.
				subprocess.run(['setxkbmap', '-layout', 'se'], check=True, capture_output=True, timeout=COMMAND_TIMEOUT)
				time.sleep(.3)
				menu_key(menu, 'ctrl+a')  # No text insertion by ordinary Ctrl chords.

				for button in ('Caps_Lock', 'a', 'Caps_Lock', 'space', 'aring', 'adiaeresis', 'odiaeresis',
					'Num_Lock', 'KP_1'):
					menu_key(menu, button)

				decision(process, log, menu, 'Return', 'Frontend resumed: main')
				decoded(args, recordings / os.fsdecode(b'a \xe5\xe4\xf61.mde'))
				subprocess.run(['setxkbmap', '-layout', 'us'], check=True, capture_output=True, timeout=COMMAND_TIMEOUT)

				finish_race(process, log, menu)
				assert len(list(recordings.glob('*.tmp.*'))) == 1
				before = {p.name: p.read_bytes() for p in recordings.iterdir() if '.tmp.' not in p.name}
				close_window(menu)  # Closing the question discards its private take.
				assert process.wait(timeout=45) == 0, diagnostic(log)
				assert not list(root.rglob('*.tmp.*'))
				assert {p.name: p.read_bytes() for p in recordings.iterdir()} == before
				print('Save flow: Cancel, editing, retry, retained names, overwrite, conflict, Swedish input and close passed', flush=True)
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
	parser.add_argument('--capture-dir', type=Path, help='optional XWD snapshots of the actual frontend')
	args = parser.parse_args()
	run(args)


if __name__ == '__main__':
	main()
