#!/usr/bin/env python3
"""Bounded secret acceptance using original assets and native consumers.

Build the normal project targets first. Exercise launch, physics, texture,
audio and Vulkan capture paths.
No original resource, player settings or record file is modified.
"""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import subprocess
import tempfile

from tests.checks.render.check_unavailable_render import bmp


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--captures', type=Path, help='keep captures here instead of a temporary directory')
	args = parser.parse_args()
	build, assets = args.build.resolve(), args.gamedir.resolve()

	def run(tool, *arguments):
		result = subprocess.run([str(build_executable(build, tool)), *map(str, arguments)],
			capture_output=True, text=True, timeout=90)

		if result.returncode:
			raise RuntimeError(f'{tool}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')

		return result.stdout

	for tool in ['test-cheats', 'test-track-flag', 'test-menu-unlocks', 'test-menu-credits', 'test-hud']:
		run(tool)

	print('Secret identities, catalog unlocks, Avenger map and secret HUD/credits checks passed', flush=True)

	with tempfile.TemporaryDirectory(prefix='mh-pc-secrets-') as temporary:
		output = args.captures.resolve() if args.captures else Path(temporary)
		output.mkdir(parents=True, exist_ok=True)

		for mode in ['room', 'road', 'helicopter']:
			result = run('mh-world-secrets-assets', assets, mode, output/mode)
			print(result.strip(), flush=True)

		run('mh-hud-secrets-assets', assets, output/'hud')
		print('Club TBL greeting/music/credits latch and Atlantika scroll passed', flush=True)

		for mode in ['--suspension', '--water', '--thunder', '--tron', '--retro']:
			run('mh-cheat-assets', assets, 1, 1, mode)

		run('mh-cheat-assets', assets, 1, 1)
		run('mh-cheat-assets', assets, 1, 1, '--supercars')
		print('Eight-car cheat physics, cameras, effects, pause and restart checks passed', flush=True)

		pairs = [('unlock', 'r pettersson', 'Swe'), ('supercars', 'Supercars', 'Grem'),
			('ignition', 'Ignition', 'UDS'), ('tron', 'tribute to tron', 'Any'),
			('moon', 'Buzz Aldrin', 'NASA'), ('avenger', 'Avenger', 'Zx'),
			('lowrider', 'g-ride', 'west'), ('springs', 'Demon', 'Grem'),
			('thunder', 'Lemmy', 'Ace'), ('water', 'Ramlosa', 'H2O'),
			('story', 'r3tr0', 'a500')]

		for label, name, team in pairs:
			path = output/f'launch-{label}.bmp'
			run('openmotor', '--debug-race', '--assets', assets, '--debug-no-settings', '--debug-no-records', '--debug-mute',
				'--debug-no-countdown', '--debug-car', 1, '--debug-track', 1, '--debug-opponents', 7,
				'--debug-ticks', 240, '--debug-name', name, '--debug-team', team, '--debug-headless', path)
			pixels, _ = bmp(path)

			if len(set(pixels)) <= 64:
				raise RuntimeError(f'{label}: empty/flat launch capture')

		print('All eleven name/team launch paths rendered with seven opponents', flush=True)

		if args.captures:
			print(f'Captures: {output}')

	print('Secret acceptance passed; helicopter check covers impact/animation, not permanent destruction.')


if __name__ == '__main__':
	main()
