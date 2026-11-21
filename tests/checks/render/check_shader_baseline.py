#!/usr/bin/env python3
"""Capture shader-sensitive paths and compare their exact rendered image bytes.

Run before and after a shader refactor on the same qualified Vulkan device.
Outputs, logs and binary identities stay outside source control. This checks
native rendering preservation, not historical GPU or cross-platform parity.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


PROFILES = ('glide', 'software', 'direct3d', 'powervr')
RACES = (
	('ordinary', 1, 1, ()),
	('patched', 16, 14, ()),
	('water', 2, 6, ('--debug-name', 'Ramlosa', '--debug-team', 'H2O')),
	('retro', 1, 1, ('--debug-name', 'r3tr0', '--debug-team', 'a500')),
	('tron', 1, 1, ('--debug-name', 'tribute to tron')),
)


def digest(path):
	return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command, output, environment):
	result = subprocess.run(list(map(str, command)), env=environment,
		capture_output=True, text=True, timeout=180)
	output.write_text(result.stdout + result.stderr)

	if result.returncode:
		raise RuntimeError(f'{command[0]} exited {result.returncode}; see {output}')


def race(build, assets, output, environment, profile, label, track, car,
	extra=(), shading='Reflection'):
	root = output / f'{profile}-{label}'
	root.mkdir()
	settings = ('DetailMode Custom\nScreenDimension 320 240\nCarView Outside\n'
		f'CarShading {shading}\nMotionBlur Off\n')
	cfg = root / 'settings.cfg'
	cfg.write_text(settings)
	command = [build_executable(build, 'openmotor'), '--debug-race', '--assets', assets, '--debug-settings', cfg,
		'--renderer', profile, '--debug-track', track, '--debug-car', car,
		'--debug-opponents', 7, '--debug-ai', 1, '--debug-ticks', 240, '--debug-no-countdown',
		'--debug-no-records', '--debug-mute',
		'--debug-capture' if profile == 'software' else '--debug-headless',
		root / 'race.bmp', *extra, '--debug-window-titles', '1']
	env = dict(environment)

	if profile == 'software':
		env.update(SDL_VIDEO_DRIVER='dummy', SDL_RENDER_DRIVER='software')

	execute(command, root / 'run.log', env)

	if cfg.read_text() != settings:
		raise RuntimeError(f'{profile}/{label}: capture rewrote settings')

	print(f'Captured {profile}/{label}', flush=True)


def focused(build, assets, output, environment):
	commands = (
		('glenz', 'mh-glenz-render', False),
		('glenz-depth', 'mh-glenz-depth', False),
		('tron-depth', 'mh-tron-render', False),
		('retro-depth', 'mh-retro-render', True),
		('history', 'mh-motion-blur-render', False),
		('fog', 'mh-fog-render', False),
	)

	for label, program, uses_assets in commands:
		root = output / label
		root.mkdir()
		command = [build_executable(build, program)]

		if uses_assets:
			command.append(assets)

		command.append(root / 'effect.bmp')
		execute(command, root / 'run.log', environment)
		print(f'Captured {label}', flush=True)

	for index, profile in enumerate(PROFILES):
		# The fixture accepts enum mh_render_profile: Glide, software, Direct3D, PowerVR.
		root = output / f'{profile}-finish'
		root.mkdir()
		execute([build_executable(build, 'mh-finish-render'), assets, index, root / 'finish'],
			root / 'run.log', environment)
		print(f'Captured {profile}/finish', flush=True)


def pictures(root):
	result = {}

	for path in sorted(p for p in root.rglob('*') if p.suffix in ('.bmp', '.tga')):
		data = path.read_bytes()

		if path.suffix == '.bmp':
			if len(data) < 54 or data[:2] != b'BM':
				raise RuntimeError(f'Invalid BMP: {path}')

			width, height = struct.unpack_from('<ii', data, 18)
		else:
			if len(data) < 18 or data[:3] != b'\0\0\2' or data[16] != 16:
				raise RuntimeError(f'Invalid TGA: {path}')

			width, height = struct.unpack_from('<HH', data, 12)

			if len(data) != 18 + width * height * 2:
				raise RuntimeError(f'Truncated TGA: {path}')

		if width <= 0 or height == 0:
			raise RuntimeError(f'Empty capture: {path}')

		result[str(path.relative_to(root))] = dict(
			sha256=digest(path), width=width, height=height, bytes=len(data))

	if not result:
		raise RuntimeError('No shader captures were produced')

	return result


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True,
		help='new directory for captures and logs')
	parser.add_argument('--reference', type=Path,
		help='earlier capture directory to compare byte for byte')
	args = parser.parse_args()
	build, assets, output = (p.resolve() for p in
		(args.build, args.gamedir, args.output))
	programs = ('openmotor', 'mh-glenz-render', 'mh-glenz-depth', 'mh-tron-render',
		'mh-retro-render', 'mh-motion-blur-render', 'mh-fog-render',
		'mh-finish-render')
	identities = {name: digest(build_executable(build, name)) for name in programs}
	output.mkdir(parents=True, exist_ok=False)
	environment = dict(os.environ, SDL_VIDEO_DRIVER='offscreen',
		SDL_GPU_DRIVER='vulkan', SDL_AUDIO_DRIVER='dummy')

	for profile in PROFILES:
		for label, track, car, extra in RACES:
			race(build, assets, output, environment, profile, label, track,
				car, extra)

		if profile in ('glide', 'direct3d'):
			race(build, assets, output, environment, profile, 'glenz', 1, 1,
				shading='Glenz')

	focused(build, assets, output, environment)
	images = pictures(output)
	manifest = dict(binaries=identities, assets=str(assets), pictures=images)
	(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

	if args.reference:
		reference = args.reference.resolve()
		expected = json.loads((reference / 'manifest.json').read_text())['pictures']

		if images != expected:
			changed = sorted(name for name in images.keys() | expected.keys()
				if images.get(name) != expected.get(name))

			raise RuntimeError('Shader captures differ: ' + ', '.join(changed))

		for name in images:
			if (output / name).read_bytes() != (reference / name).read_bytes():
				raise RuntimeError(f'Shader capture bytes differ: {name}')

		print(f'{len(images)} shader captures match the reference byte for byte')
	else:
		print(f'Recorded {len(images)} shader reference captures in {output}')


if __name__ == '__main__':
	main()
