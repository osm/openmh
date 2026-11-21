#!/usr/bin/env python3
"""Exercise the selectable 3.1 Direct3D profile; not historical driver pixel parity."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--baseline', type=Path, help='previous debug openmotor for default GPU regression')
	parser.add_argument('--captures', type=Path, help='retain race BMPs for inspection')
	args = parser.parse_args()
	env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen')

	if args.captures is not None:
		args.captures.mkdir(parents=True, exist_ok=True)

	with tempfile.TemporaryDirectory(prefix='mh-direct3d-lighting-') as directory:
		root = Path(directory)

		for tool, output in (('mh-direct3d-faces', 'faces.bmp'),
			('mh-atmosphere-render', 'atmosphere'),
			('mh-shadow-render', 'shadow'),
			('mh-direct3d-skids', 'skids.bmp'),
			('mh-direct3d-effects', 'effects.bmp'),
			('mh-direct3d-materials', 'materials.bmp'),
			('mh-direct3d-sampling', 'sampling.bmp'),
			('mh-texture-filter', 'filter.bmp'),
			('mh-motion-blur-render', 'blur')):
			result = subprocess.run([str(build_executable(args.build.resolve(), tool)),
				str(root / output)], env=env,
				capture_output=True, text=True, timeout=60)
			assert result.returncode == 0, (result.stdout, result.stderr)
			print(result.stdout.strip())

		cfg = root / 'race.cfg'
		common = [build_executable(args.build.resolve(), 'openmotor'), '--debug-race', '--assets', args.gamedir.resolve(),
			'--debug-settings', cfg, '--debug-opponents', '7', '--debug-ai', '1',
			'--debug-no-countdown', '--debug-no-records', '--debug-window-titles', '1']

		def capture(renderer, track=1, car=1, shading='Reflection', ticks=60,
			executable=None, name=None, team=None, blur=False,
			filtering=None, precision=None):
			settings = ('DetailMode Custom\nScreenDimension 320 240\n'
				f'CarView Outside\nCarShading {shading}\n'
				f'MotionBlur {"On" if blur else "Off"}\n')

			if filtering is not None:
				settings += f'TextureFiltering {filtering}\n'

			if precision is not None:
				settings += f'TexturePrecision {precision}\n'

			cfg.write_text(settings)
			output = root / 'race.bmp'
			output.unlink(missing_ok=True)
			current = dict(env)

			if renderer == 'software':
				# CPU presentation needs no offscreen GL/EGL driver.
				current['SDL_VIDEO_DRIVER'] = 'dummy'
				current['SDL_RENDER_DRIVER'] = 'software'

			command = common + ['--renderer', renderer, '--debug-track', str(track),
				'--debug-car', str(car), '--debug-ticks', str(ticks),
				'--debug-capture' if renderer == 'software' else '--debug-headless', output]

			if executable is not None:
				command[0] = executable.resolve()

			if name is not None:
				command += ['--debug-name', name]

			if team is not None:
				command += ['--debug-team', team]

			result = subprocess.run(list(map(str, command)), env=current,
				capture_output=True, text=True, timeout=180)
			assert result.returncode == 0, (command, result.stdout, result.stderr)
			profile = {'glide': 'Glide', 'direct3d': 'Direct3D', 'software': 'Software'}[renderer]
			assert f'Race renderer: {profile}' in result.stdout, result.stdout
			assert 'Diagnostic 3.1 material lighting enabled' not in result.stderr
			assert cfg.read_text() == settings
			assert output.is_file() and output.stat().st_size > 1000
			data = output.read_bytes()

			if args.captures is not None:
				filename = f'{renderer}-{track}-{car}-{shading}-{ticks}'

				if blur:
					filename += '-blur'

				if filtering is not None:
					filename += f'-{filtering}-{precision}'

				if name is not None:
					filename += '-' + name.replace(' ', '-')

				if executable is not None:
					filename += '-baseline'

				(args.captures / (filename + '.bmp')).write_bytes(data)

			return data

		baseline = capture('glide')

		if args.baseline is not None:
			assert capture('glide', executable=args.baseline) == baseline

		assert capture('direct3d') != baseline, 'Direct3D failed to alter the rendered race'
		software = capture('software')
		capture('direct3d', shading='Glenz')
		capture('direct3d', track=16, car=14)
		capture('direct3d', track=2, car=6, shading='Glenz', ticks=240)
		capture('direct3d', name='r3tr0', team='a500')
		capture('direct3d', name='tribute to tron')

		for name, team in [('Ramlosa', 'H2O'), ('Lemmy', 'Ace')]:
			weather = capture('direct3d', name=name, team=team)
			assert weather != capture('direct3d'), name

		for name, team in [(None, None), ('Ramlosa', 'H2O'),
			('Lemmy', 'Ace'), ('Ignition', 'UDS')]:
			clear = capture('direct3d', name=name, team=team)
			blurred = capture('direct3d', name=name, team=team, blur=True)

			# Headless capture draws once after simulation: black sky over
			# black startup remains black, even with zero source alpha.
			if name == 'Ignition':
				assert clear == blurred, 'black startup sky changed under blur'
			else:
				assert clear != blurred, ('blur did not change the race', name)

		by_precision = []

		for precision in (16, 32):
			pictures = [capture('direct3d', track=2, shading='Glenz', ticks=240,
				filtering=mode, precision=precision)
				for mode in ('Original', 'Nearest', 'Bilinear', 'Trilinear')]
			assert pictures[0] == pictures[2], 'Direct3D Original must use bilinear opaque sampling'
			assert pictures[1] != pictures[2], 'Direct3D nearest/bilinear identical'
			assert pictures[2] != pictures[3], 'Direct3D trilinear inactive'
			by_precision.append(pictures)

		assert all(low != full for low, full in zip(*by_precision)), 'Direct3D precision inactive'

	print('Direct3D profile: thirty-one race captures pass; GPU colors change, default/software isolation '
		'and saved settings hold; Glenz, moving geometry, retro/TRON, underwater/thunder '
		'and patched car/course render; both precisions and all Direct3D filters pass')


if __name__ == '__main__':
	main()
