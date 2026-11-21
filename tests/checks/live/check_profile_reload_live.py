#!/usr/bin/env python3
"""Check ADP rereads and their RNG/AI/results/audio consumers across X11 races.

Copies mutable assets into a private tree, supplies known horn names so no
fallback reseed can hide missing draws, then edits/adds/removes ADPs during
sessions. Installed assets are never modified. Run under xvfb-run.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from tests.checks.live.check_league_reload_live import attack, leave
from tests.checks.live.check_menu_controls_live import snapshot
from tests.checks.support.application import frontend
from tests.checks.support.catalog import fields
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.settings import encode, read_cfg
from tests.checks.support.x11 import frames, key, wait_frames, xdo


def private_assets(source, root):
	root.mkdir()

	for entry in source.resolve().iterdir():
		target = root/entry.name

		if entry.name == 'Game':
			shutil.copytree(entry, target)
		elif entry.name == 'League':
			target.mkdir()

			for child in entry.iterdir():
				if child.name == 'Profiles':
					shutil.copytree(child, target/child.name)
				else:
					(target/child.name).symlink_to(child, target_is_directory=child.is_dir())
		else:
			target.symlink_to(entry, target_is_directory=entry.is_dir())


def write_profiles(build, directory, texts, name, aggression, horn, extra):
	for filename, text in texts.items():
		# Shipped ADPs misspell this key, so the original ignores it and keeps
		# .5. Use the recognized spelling in the private edited fixture.
		text = re.sub(r'(?m)^Aggressivness\b', 'Aggressiveness', text)

		for field, value in (('PlayerName', name), ('Aggressiveness', str(aggression)),
			('Eagerness', str(100-aggression)), ('CarHorn', horn)):
			text, count = re.subn(r'(?m)^'+field+r'[^\S\n]+[^\n]*', field+'\t'+value, text)
			assert count == 1, (filename, field, count)

		(directory/filename).write_bytes(subprocess.check_output(
			[str(build_executable(build, 'test-car')), '--tbl-encode', ''], input=text.encode('latin1'), timeout=30))

	added = directory/'ReloadExtra.adp'
	assert added.name not in texts

	if extra:
		text, count = re.subn(r'(?m)^PlayerNick[^\S\n]+[^\n]*', 'PlayerNick\tReload extra', text)
		assert count == 1
		added.write_bytes(subprocess.check_output([str(build_executable(build, 'test-car')), '--tbl-encode', ''],
			input=text.encode('latin1'), timeout=30))
	elif added.exists():
		added.unlink()


def catalog_traces(log, counts):
	rows = re.findall(r'^Profile catalog: (\d+) entries, RNG (\d+) -> (\d+)$', log.read_text(), re.M)
	assert [int(row[0]) for row in rows] == counts, diagnostic(log)

	for count, before, after in rows:
		count, seed = int(count), int(before)

		# Original portrait fill, then one constructor draw per ADP. Known
		# CarHorn values select directly without additional draws/reseeding.
		for _ in range((count+1)*13824+count):
			seed = (seed*1103515245+12345)&0xffffffff

		assert seed == int(after), (count, before, after, seed)


def race_profiles(process, log, name, aggression, horn):
	window = window_for(process, log, r'\| 8 cars \|')
	xdo('windowfocus', '--sync', window)
	wait_frames(process, log, frames(log), 6)
	launch = log.read_text().rsplit('Race launch:', 1)[-1]
	rows = re.findall(r'^Race profile: slot (\d+), source (\d+), name (.*), '
		r'aggression ([\d.]+), eagerness ([\d.]+), horn (.*)$', launch, re.M)
	assert [int(row[0]) for row in rows] == list(range(1, 8)), diagnostic(log)

	for slot, source, actual_name, actual_aggression, eagerness, actual_horn in rows:
		assert (actual_name, float(actual_aggression), float(eagerness), actual_horn) == (
			name, aggression/100, (100-aggression)/100, horn), rows

	return window


def finish(process, window, log):
	before = log.read_text().count('Frontend resumed: main')

	for button in ('Escape', 'Up'):
		key(window, button, process, log)

	xdo('keydown', 'Return')

	try:
		wait(process, log, lambda: log.read_text().count('Frontend resumed: main') > before)
	finally:
		xdo('keyup', 'Return')


def run(args, root):
	assets = root/'assets'
	private_assets(args.gamedir, assets)
	profiles = assets/'League/Profiles'
	texts = {p.name: subprocess.check_output([str(build_executable(args.build, 'mh-tbl-decode')), str(p)],
		timeout=30).decode('latin1').replace('\r\n', '\n') for p in profiles.glob('*.adp')}
	assert len(texts) >= 26
	write_profiles(args.build, profiles, texts, 'Initial driver', 25, 'Honker1', False)
	cfg, log = root/'test.cfg', root/'live.log'
	cfg.write_bytes(encode(dict(PlayerName='ADP reload', TeamName='TEST',
		CarName=fields(assets/'Game/car01.car')['carname'],
		TrackName=fields(assets/'Game/Track1.trk')['trackname'],
		GameType='TimeAttack', RecordRace='Off', MenuConfirmation='Off',
		TrackLaps='1', SFXVolume='0', MusicVolume='0', DetailMode='Custom', ViewDistance='40',
		ScreenDimension='640 400')))
	command = [str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--assets', str(assets),
		'--debug-settings', str(cfg), '--debug-no-records', '--audio', '0', '--debug-recordings', str(root/'Recordings'),
		'--debug-camera-trace', '1',
		'--debug-profile-trace', '1',
		'--debug-window-titles', '1']
	env = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')
	n = len(texts)

	with frontend(command, ['--debug-page', 'single'], log, env) as (process, menu):
		catalog_traces(log, [n])
		menu_key(menu, 'Return')
		window = attack(process, log)
		write_profiles(args.build, profiles, texts, 'Reloaded driver', 75, 'Honker2', True)
		values = read_cfg(cfg)
		values['GameType'] = 'Single'
		cfg.write_bytes(encode(values))
		leave(process, window, log)
		catalog_traces(log, [n, n+1])

		for button in ('Return', 'Return'):
			menu_key(menu, button)

		window = race_profiles(process, log, 'Reloaded driver', 75, 'Honker2')
		catalog_traces(log, [n, n+1])
		print('ADP reload: Time Attack return rereads added/edited files and RNG; Quick Race uses refreshed AI, results names and horn bindings', flush=True)
		write_profiles(args.build, profiles, texts, 'Updated again', 25, 'Honker1', False)
		finish(process, window, log)
		catalog_traces(log, [n, n+1, n])

		for button in ('Return', 'Return'):
			menu_key(menu, button)

		window = race_profiles(process, log, 'Updated again', 25, 'Honker1')
		finish(process, window, log)
		catalog_traces(log, [n, n+1, n, n])

	catalog_traces(log, [n, n+1, n, n])
	print('ADP reload: removal, second refreshed Quick Race, all shared-RNG prefixes and clean shutdown passed', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	directories = [args.gamedir/'Game', args.gamedir/'League/Profiles']
	before = [snapshot(directory) for directory in directories]

	try:
		with tempfile.TemporaryDirectory(prefix='mh-profile-reentry-') as temporary:
			run(args, Path(temporary))
	finally:
		assert [snapshot(directory) for directory in directories] == before, 'installed assets changed'


if __name__ == '__main__':
	main()
