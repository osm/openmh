#!/usr/bin/env python3
"""Verify native texture filtering and optional patched race settings.

No legacy Direct3D driver pixel parity is claimed. CPU mip arithmetic has
separate unit tests.
"""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import tempfile

from tests.checks.render.check_glenz_render import run


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path)
	parser.add_argument('--renderer', choices=('glide', 'direct3d'), default='glide')
	parser.add_argument('--isolation-only', action='store_true',
		help='check default/Original and software isolation without repeating GPU cases')
	args = parser.parse_args()

	if args.isolation_only and args.gamedir is None:
		parser.error('--isolation-only requires --gamedir')

	build = args.build.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-texture-filter-') as tmp:
		root = Path(tmp)

		if not args.isolation_only:
			print(run([build_executable(build, 'mh-texture-filter'), root / 'filter.bmp']).strip(), flush=True)
			print(run([build_executable(build, 'mh-glenz-render'), root / 'glenz.bmp']).strip(), flush=True)

		if args.gamedir is None:
			return

		env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen')
		cfg = root / 'settings.cfg'
		common = [build_executable(build, 'openmotor'), '--debug-race', '--assets', args.gamedir.resolve(),
			'--debug-settings', cfg, '--debug-car', '1', '--debug-opponents', '7',
			'--debug-ai', '1', '--debug-no-countdown', '--debug-ticks', '240', '--debug-no-records', '--debug-window-titles', '1']
		base = 'DetailMode Custom\nScreenDimension 320 240\nCarView Outside\nCarShading Glenz\n'
		modes = ('Original', 'Nearest', 'Bilinear', 'Trilinear')

		for track in (() if args.isolation_only else (1, 2, 16)):
			by_precision = []

			for precision in (16, 32):
				pictures = []

				for mode in modes:
					text = base + f'TextureFiltering {mode}\nTexturePrecision {precision}\n'
					cfg.write_text(text)
					capture = root / f'{mode}.bmp'
					run(common + ['--renderer', args.renderer, '--debug-track', track, '--debug-headless', capture], env)
					assert cfg.read_text() == text
					pictures.append(capture.read_bytes())

				assert pictures[1] != pictures[2], f'nearest/bilinear identical on track {track}'
				assert pictures[2] != pictures[3], f'trilinear inactive on track {track}'
				by_precision.append(pictures)

			assert all(low != full for low, full in zip(*by_precision)), track

		if not args.isolation_only:
			print(f'{args.renderer}: both precisions / all filters on courses 1/2/16, shared atlases and seven opponents passed', flush=True)

		for renderer in (args.renderer, 'software'):
			captures = []

			# CPU presentation needs no offscreen OpenGL/EGL driver.
			current = (dict(env, SDL_VIDEO_DRIVER='dummy', SDL_RENDER_DRIVER='software')
				if renderer == 'software' else env)

			for mode in (None, *modes):
				text = base + (f'TextureFiltering {mode}\n' if mode else '')
				cfg.write_text(text)
				capture = root / 'isolation.bmp'
				run(common + ['--renderer', renderer, '--debug-track', '1',
					'--debug-capture' if renderer == 'software' else '--debug-headless', capture], current)
				captures.append(capture.read_bytes())
				assert cfg.read_text() == text

			assert captures[0] == captures[1], renderer

			if renderer == 'software':
				assert all(picture == captures[0] for picture in captures)

		print('Default equals Original; software ignores all GPU filter choices and preserves CFG', flush=True)


if __name__ == '__main__':
	main()
