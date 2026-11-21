#!/usr/bin/env python3
"""Compare all renderer profiles with a second debug OpenMH executable."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--baseline', type=Path, required=True)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-renderer-comparison-') as directory:
		root = Path(directory)
		cfg = root / 'settings.cfg'
		settings = ('DetailMode Custom\nScreenDimension 320 240\nCarView Outside\n'
			'CarShading Reflection\nMotionBlur Off\n')
		cfg.write_text(settings)
		count = 0

		for profile in ('software', 'glide', 'direct3d', 'powervr'):
			env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_GPU_DRIVER='vulkan')
			capture = '--debug-headless'

			if profile == 'software':
				env.update(SDL_VIDEO_DRIVER='dummy', SDL_RENDER_DRIVER='software')
				capture = '--debug-capture'

			for label, track, car, extra in (
				('ordinary', 1, 1, []),
				('patched', 16, 14, []),
				('water', 2, 6, ['--debug-name', 'Ramlosa', '--debug-team', 'H2O']),
				('retro', 1, 1, ['--debug-name', 'r3tr0', '--debug-team', 'a500']),
				('tron', 1, 1, ['--debug-name', 'tribute to tron'])):
				pictures = []

				for executable in (args.baseline, build_executable(args.build, 'openmotor')):
					output = root / 'race.bmp'
					output.unlink(missing_ok=True)
					command = [executable.resolve(), '--debug-race', '--assets', args.gamedir.resolve(),
						'--debug-settings', cfg, '--renderer', profile, '--debug-track', track,
						'--debug-car', car, '--debug-opponents', 7, '--debug-ai', 1, '--debug-ticks', 240,
						'--debug-no-countdown', '--debug-no-records', '--debug-mute', capture, output,
						*extra]
					result = subprocess.run(list(map(str, command)), env=env,
						capture_output=True, text=True, timeout=180)
					assert result.returncode == 0, (profile, label, result.stdout, result.stderr)
					assert cfg.read_text() == settings, 'capture rewrote settings'
					data = output.read_bytes()
					assert data[:2] == b'BM' and len(data) > 1000
					pictures.append(data)

				assert pictures[0] == pictures[1], (profile, label, 'rendered bytes changed')
				count += 1
				print(f'{profile} {label}: identical capture', flush=True)

	print(f'{count} renderer captures match the baseline executable byte for byte')


if __name__ == '__main__':
	main()
