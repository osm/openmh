#!/usr/bin/env python3
"""Prepare, relocate and launch demos through the installed openmotorex/openmotor commands.

Exercise actual opening EOF/skip, menu-to-race and return with isolated saves.
Only the prepared tree remains at runtime; the temporary installer is removed.
Run under Xvfb with xdotool and the ordinary SDL/Vulkan runtime dependencies.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from tests.checks.live.check_demo_menus_live import stop
from tests.checks.live.check_demo_openings import INTROS, WITHOUT_INTRO
from tests.checks.support.menu import diagnostic, menu_key, wait, window_for
from tests.checks.support.x11 import close_window, frames, key, wait_frames, xdo


def wait_menu(process, log):
	windows = []

	def visible():
		result = subprocess.run(['xdotool', 'search', '--all', '--onlyvisible',
			'--pid', str(process.pid), '--name', 'main-menu preview'],
			capture_output=True, text=True, timeout=10)

		if result.returncode == 0 and result.stdout.strip():
			windows.append(int(result.stdout.splitlines()[-1]))

		return bool(windows)

	wait(process, log, visible, seconds=240)

	return windows[0]


def check_launch(client, content, work, reports, count, skip):
	label = content.parent.name + ('-skip' if skip else '-end')
	log = reports / (label + '.log')
	userdata = content / 'userdata'
	environment = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy', PATH=str(client.parent))

	with log.open('w') as stream:
		process = subprocess.Popen([str(client), '--assets', str(content),
			'--debug-camera-trace', '1',
			'--debug-window-titles', '1'],
			cwd=work, env=environment, stdout=stream, stderr=stream)

		try:
			if count:
				# SDL may replace its first window when the video canvas opens.
				time.sleep(1)
				window = window_for(process, log, '^Motorhead movie$')
				assert not userdata.exists(), 'intro opened settings early'

				if skip:
					menu_key(window, 'Escape')
				else:
					xdo('windowsize', window, 800, 600)

			menu = wait_menu(process, log)
			time.sleep(.5)
			text = log.read_text()
			clips = [int(line.split(': ')[1]) for line in text.splitlines()
				if line.startswith('Opening movie: ')]
			assert clips == list(range(1 if skip else count)), diagnostic(log)
			assert 'Race launch:' not in text, 'movie skip leaked into race launch'

			for failure in ('Movie playback:', 'Opening movies:', 'AddressSanitizer', 'runtime error:'):
				assert failure not in text, diagnostic(log)

			menu_key(menu, 'Return')
			race = window_for(process, log, r'\| 8 cars \|')
			xdo('windowfocus', '--sync', race)
			wait_frames(process, log, frames(log), 6)

			for name in ('Escape', 'Up'):
				key(race, name, process, log)

			xdo('keydown', 'Return')

			try:
				wait(process, log, lambda: 'Frontend resumed: main' in log.read_text())
			finally:
				xdo('keyup', 'Return')

			assert window_for(process, log, 'main-menu preview') == menu
			close_window(menu)
			assert process.wait(timeout=30) == 0, diagnostic(log)

			for failure in ('AddressSanitizer', 'runtime error:'):
				assert failure not in log.read_text(), diagnostic(log)

			expected = userdata / 'motorhead.cfg'
			assert expected.is_file(), (expected, diagnostic(log))
			assert len(list(userdata.rglob('motorhead.cfg'))) == 1
			assert list(content.rglob('motorhead.cfg')) == [expected]
		except Exception:
			print(diagnostic(log), flush=True)

			raise
		finally:
			stop(process)

	print(f'{label}: {len(clips)} opening clips, menu, 8-car race, return, '
		'isolated save; source absent and cwd unrelated', flush=True)


def prepare(binaries, source, parent, reports):
	package = parent / 'installer.exe'
	shutil.copyfile(source, package)
	output = parent / 'prepared'
	result = subprocess.run([str(binaries / 'openmotorex'), str(package), str(output)],
		cwd=parent, capture_output=True, text=True, timeout=120)
	(reports / (source.stem + '-prepare.log')).write_text(result.stdout + result.stderr)
	assert result.returncode == 0, result.stderr
	package.unlink()
	moved = parent / 'moved content'
	output.rename(moved)
	assert not package.exists() and not output.exists()

	return moved


def copy_installed_client(binaries, work):
	"""Relocate the installed client and its bundled libraries without tools."""

	runtime = work / 'bin'
	runtime.mkdir()
	client = runtime / 'openmotor'
	shutil.copy2(binaries / 'openmotor', client)
	libraries = binaries.parent / 'lib'

	if libraries.is_dir():
		shutil.copytree(libraries, work / 'lib')

	return client


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--bin', type=Path, required=True,
		help='Directory containing installed openmotorex and openmotor')
	parser.add_argument('--demos', type=Path, required=True)
	parser.add_argument('--reports', type=Path,
		default=Path('build/checks/compatibility/demos/prepared-startup'))
	parser.add_argument('--skip-full', action='store_true')
	args = parser.parse_args()
	binaries = args.bin.resolve()
	reports = args.reports.resolve()
	reports.mkdir(parents=True, exist_ok=True)
	editions = [(edition, count) for edition, _, count in INTROS]
	editions += [(edition, 0) for edition in WITHOUT_INTRO]

	with tempfile.TemporaryDirectory(prefix='mh-installed-demos-') as temporary:
		work = Path(temporary)

		# Ship only the installed client into runtime PATH, without openmotorex/tools.
		client = copy_installed_client(binaries, work)

		for edition, count in editions:
			parent = work / edition
			parent.mkdir()
			content = prepare(binaries, args.demos.resolve() / (edition + '.exe'), parent, reports)

			if not args.skip_full or count == 0:
				check_launch(client, content, work, reports, count, False)

			if count:
				shutil.rmtree(content / 'userdata', ignore_errors=True)
				check_launch(client, content, work, reports, count, True)

			shutil.rmtree(parent)

	print('Prepared demo startup checks passed; playback uses dummy audio, not listening review.')


if __name__ == '__main__':
	main()
