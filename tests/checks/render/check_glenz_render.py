#!/usr/bin/env python3
"""Check native Glenz GPU passes and optionally patched race assets.

This exercises native integration, not original Direct3D rasterization parity.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


from tests.checks.support.process import COMMAND_TIMEOUT


def run(command, env=None):
	result = subprocess.run([str(arg) for arg in command], env=env,
		text=True, capture_output=True, timeout=COMMAND_TIMEOUT)

	if result.returncode:
		raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')

	return result.stdout


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path,
		help='optional prepared 3.1 installation (14 cars, 16 courses)')
	args = parser.parse_args()
	build = args.build.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-glenz-render-') as directory:
		root = Path(directory)
		print(run([build_executable(build, 'mh-glenz-render'), root / 'material.bmp']).strip())
		print(run([build_executable(build, 'mh-glenz-depth'), root / 'depth.bmp']).strip())

		if args.gamedir is None:
			return

		print(run([build_executable(build, 'mh-glenz-lifecycle'), args.gamedir.resolve()]).strip())
		env = dict(os.environ)
		common = [build_executable(build, 'openmotor'), '--debug-race', '--assets', args.gamedir.resolve(),
			'--debug-no-settings', '--debug-opponents', '7', '--debug-ai', '1',
			'--debug-no-countdown', '--debug-ticks', '240', '--debug-window-titles', '1']

		for track in range(1, 17):
			car = (track - 1) % 14 + 1
			command = common + ['--renderer', 'glide', '--debug-car', car,
				'--debug-track', track, '--debug-headless', root / 'race.bmp']
			run(command + ['--debug-glenz', '1'], env)

		print('Glenz: 16 patched courses / all 14 player cars rendered with seven opponents')

		for renderer in ('glide', 'software'):
			# The default GPU path is independent of the diagnostic override;
			# the software profile must ignore it altogether.
			for option in ('unset', '0', '1'):
				if renderer == 'glide' and option == '1':
					continue

				current = dict(os.environ, SDL_VIDEO_DRIVER='offscreen')
				extra = [] if option == 'unset' else ['--debug-glenz', option]
				run(common + ['--renderer', renderer, '--debug-car', '1', '--debug-track', '1',
					'--debug-capture' if renderer == 'software' else '--debug-headless',
					root / f'{option}.bmp'] + extra, current)

			assert (root / 'unset.bmp').read_bytes() == (root / '0.bmp').read_bytes()

			if renderer == 'software':
				assert (root / 'unset.bmp').read_bytes() == (root / '1.bmp').read_bytes()

		print('Glenz: default GPU and software diagnostic isolation captures match exactly')

		# A saved choice reaches the same material as the diagnostic override.
		# Software explicitly falls back to Reflection when loading that CFG.
		cfg = root / 'glenz.cfg'
		saved = [build_executable(build, 'openmotor'), '--debug-race', '--assets', args.gamedir.resolve(),
			'--debug-settings', cfg, '--debug-car', '1', '--debug-track', '1', '--debug-opponents', '7',
			'--debug-ai', '1', '--debug-no-countdown', '--debug-ticks', '240', '--debug-no-records', '--debug-window-titles', '1']
		base = 'DetailMode Custom\nScreenDimension 320 240\nCarView Outside\n'
		current = dict(os.environ, SDL_VIDEO_DRIVER='offscreen')

		for renderer in ('glide', 'software'):
			for shading in ('Reflection', 'Glenz'):
				cfg.write_text(base + f'CarShading {shading}\n')
				extra = ['--debug-glenz', '1'] if renderer == 'glide' and shading == 'Reflection' else []
				output = root / f'{shading}.bmp'
				run(saved + ['--renderer', renderer,
					'--debug-headless' if renderer == 'glide' else '--debug-capture', output] + extra,
					current)
				assert cfg.read_text() == base + f'CarShading {shading}\n'

			assert (root / 'Reflection.bmp').read_bytes() == (root / 'Glenz.bmp').read_bytes()

		print('Glenz: saved GPU choice matches diagnostic; software fallback matches Reflection')

		for renderer, shading in (('glide', 'Glenz'), ('glide', 'Reflection'),
			('software', 'Glenz')):
			for precision in ('default', '16', '32'):
				text = base + f'CarShading {shading}\n'

				if precision != 'default':
					text += f'TexturePrecision {precision}\n'

				cfg.write_text(text)
				run(saved + ['--renderer', renderer,
					'--debug-headless' if renderer == 'glide' else '--debug-capture',
					root / f'precision-{precision}.bmp'], current)
				assert cfg.read_text() == text

			full = (root / 'precision-32.bmp').read_bytes()
			reduced = (root / 'precision-16.bmp').read_bytes()
			assert (root / 'precision-default.bmp').read_bytes() == full

			if renderer == 'glide':
				assert reduced != full, 'Texture precision did not reach GPU textures'
			else:
				assert reduced == full, (renderer, shading, 'precision leaked to software')

		print('Texture precision: ordinary/Glenz GPU frames change; default equals 32-bit; software stays identical')


if __name__ == '__main__':
	main()
