#!/usr/bin/env python3
"""Exercise public PowerVR races and fixed profile policy with patched assets."""

from tests.checks.support.build import build_executable
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--captures', type=Path, help='retain BMPs for inspection')
	args = parser.parse_args()
	env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_GPU_DRIVER='vulkan')

	if args.captures:
		args.captures.mkdir(parents=True, exist_ok=True)

	with tempfile.TemporaryDirectory(prefix='mh-powervr-profile-') as directory:
		root = Path(directory)
		cfg = root / 'settings.cfg'
		captures = 0

		def capture(label, track=1, car=1, shading='Reflection', ticks=60,
			name=None, team=None, blur=False, filtering='Original', precision=16):
			nonlocal captures
			settings = ('Renderer PowerVR\nDetailMode Custom\nScreenDimension 320 240\n'
				f'CarView Outside\nCarShading {shading}\n'
				f'MotionBlur {"On" if blur else "Off"}\n'
				f'TextureFiltering {filtering}\nTexturePrecision {precision}\n')
			cfg.write_text(settings)
			output = root / 'race.bmp'
			output.unlink(missing_ok=True)
			command = [build_executable(args.build.resolve(), 'openmotor'), '--debug-race', '--assets', args.gamedir.resolve(),
				'--debug-settings', cfg, '--debug-opponents', '7', '--debug-ai', '1', '--debug-mute',
				'--debug-no-countdown', '--debug-no-records', '--debug-track', track, '--debug-car', car,
				'--debug-ticks', ticks, '--debug-headless', output, '--debug-window-titles', '1']

			if name is not None:
				command += ['--debug-name', name]

			if team is not None:
				command += ['--debug-team', team]

			result = subprocess.run(list(map(str, command)), env=env,
				capture_output=True, text=True, timeout=180)
			assert result.returncode == 0, (label, result.stdout, result.stderr)
			assert 'Race renderer: PowerVR' in result.stdout, result.stdout
			assert cfg.read_text() == settings, 'capture rewrote saved preferences'
			data = output.read_bytes()
			assert data[:2] == b'BM'
			width, height = struct.unpack_from('<ii', data, 18)
			assert width == 320 and abs(height) == 240, (width, height)
			offset = struct.unpack_from('<I', data, 10)[0]
			assert len(set(data[offset:])) > 64, (label, 'empty/flat capture')

			if args.captures:
				(args.captures / (label + '.bmp')).write_bytes(data)

			captures += 1
			print(f'PowerVR capture: {label}', flush=True)

			return data

		reference = capture('track-1')

		for track in range(2, 9):
			capture(f'track-{track}', track=track, ticks=240)

		capture('reverse-track-1', track=9)
		capture('patched-car-course', track=16, car=14)
		assert capture('glenz-normalized', shading='Glenz') == reference
		assert capture('blur-ignored', blur=True) == reference
		assert capture('nearest-ignored', filtering='Nearest') == reference
		assert capture('trilinear-32-ignored', filtering='Trilinear', precision=32) == reference

		for label, name, team in (('retro', 'r3tr0', 'a500'),
			('tron', 'tribute to tron', 'Any'),
			('water', 'Ramlosa', 'H2O'),
			('thunder', 'Lemmy', 'Ace'),
			('ignition', 'Ignition', 'UDS')):
			assert capture(label, name=name, team=team) != reference, label

	print(f'PowerVR profile: {captures} eight-car captures pass; fixed blur/filter/precision '
		'policy, Glenz normalization, saved preferences, tracks, patched content and secrets')


if __name__ == '__main__':
	main()
