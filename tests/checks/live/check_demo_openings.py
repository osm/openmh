#!/usr/bin/env python3
"""Play relocated demo intros to EOF and exercise real window/input teardown."""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from tests.checks.live.check_menu_credits_live import capture
from tests.checks.support.menu import menu_key, window_for


INTROS = (('motordemo_soft_intro', 'demo.pak', 2),
	('motordemo_3dfx_intro', 'demo.pak', 2),
	('motordemo2_3dfx_intro', 'demo2.pak', 4))
WITHOUT_INTRO = ('motordemo_soft', 'motordemo2_soft',
	'motordemo_3dfx', 'motordemo_mplayer')


def run_sequence(binary, edition, root, reports, count, skip):
	label = edition + ('-skip' if skip else '-end')
	log = reports / (label + '.log')
	environment = dict(os.environ, SDL_VIDEO_DRIVER='x11', SDL_AUDIO_DRIVER='dummy')

	with log.open('w') as stream:
		process = subprocess.Popen([str(binary), edition, str(root)], cwd=root.parent,
			stdout=stream, stderr=stream, env=environment)

		try:
			if count:
				# Canvas initialization may replace the first SDL window.
				# Resolve the live handle after the first frame has settled.
				time.sleep(1)
				window = window_for(process, log, '^Motorhead movie$')

				if skip:
					menu_key(window, 'Escape')
				else:
					subprocess.run(['xdotool', 'windowsize', str(window), '800', '600'],
						check=True, timeout=10)
					time.sleep(.4)
					capture(window, reports / (label + '.png'))

			assert process.wait(timeout=180) == 0, log.read_text()
		finally:
			if process.poll() is None:
				process.terminate()

				try:
					process.wait(timeout=5)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)

	text = log.read_text()
	expected = list(range(1 if skip else count))
	actual = [int(line.split(': ')[1]) for line in text.splitlines()
		if line.startswith('Opening movie: ')]
	assert actual == expected, text
	assert f'Opening outcome: {2 if skip else 1}\n' in text, text

	for failure in ('unavailable', 'not found', 'movie playback:', 'failed', 'error:'):
		assert failure not in text.lower(), text

	print(f'{edition}: {"skip" if skip else "EOF"}, {len(actual)} clips, clean exit', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--skip-full', action='store_true',
		help='Run skip/absent-intro checks without waiting for natural EOF')
	parser.add_argument('--reports', type=Path,
		default=Path('build/checks/compatibility/demos/openings'))
	args = parser.parse_args()
	binary = (build_executable(args.build, 'mh-content-opening-check')).resolve()
	reports = args.reports.resolve()
	reports.mkdir(parents=True, exist_ok=True)

	with tempfile.TemporaryDirectory(prefix='mh-demo-openings-') as temporary:
		parent = Path(temporary)

		for edition, archive, count in INTROS:
			source = args.gamedir / edition / 'Movies' / archive
			digest = hashlib.sha256(source.read_bytes()).hexdigest()
			staging = parent / 'staging'
			(staging / 'Movies').mkdir(parents=True)
			shutil.copyfile(source, staging / 'Movies' / archive)
			relocated = parent / edition
			staging.rename(relocated)

			if not args.skip_full:
				run_sequence(binary, edition, relocated, reports, count, False)

			run_sequence(binary, edition, relocated, reports, count, True)
			assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
			shutil.rmtree(relocated)

		for edition in WITHOUT_INTRO:
			run_sequence(binary, edition, parent / 'absent', reports, 0, False)

	print('Requested demo intro checks pass; automated playback is not human audio/visual review.')


if __name__ == '__main__':
	main()
