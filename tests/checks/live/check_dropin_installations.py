#!/usr/bin/env python3
"""Check copied-client startup in supplied game installations under Xvfb.

--retail selects an unpatched European installation. --demos contains one
installation directory per demo. Patch inputs come from an extracted 3.0
installer and the European 3.1 ZIP. All changes use temporary copies.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from tests.checks.support.build import build_executable


def copy_file(source, destination):
	destination.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(source, destination)


def patch_destination(root, relative):
	# Model Windows overwrite semantics without creating Motions/motions peers.
	for part in relative.parts:
		matches = [p for p in root.iterdir() if p.name.lower() == part.lower()] if root.is_dir() else []

		if len(matches) > 1:
			raise ValueError(f'Ambiguous original patch destination: {root / part}')

		root = matches[0] if matches else root / part

	return root


def patched_installation(base, destination, components, patch31=None):
	shutil.copytree(base, destination)

	for source in sorted((components / 'Data').rglob('*')):
		if source.is_file():
			copy_file(source, patch_destination(destination, source.relative_to(components / 'Data')))

	copy_file(components / 'Program Executable Files/motor.exe', destination / 'motor.exe')

	if patch31 is not None:
		with zipfile.ZipFile(patch31) as archive:
			name = next(name for name in archive.namelist() if name.lower() == 'motor.exe')
			(destination / 'motor.exe').write_bytes(archive.read(name))


def check_startup(binary, installation, cwd, reports):
	client = installation / 'openmotor'
	copy_file(binary, client)
	capture = reports / (installation.name + '.bmp')

	# A previous report must never count as a successful new capture.
	capture.unlink(missing_ok=True)
	command = [str(client), '--debug-no-intro', '--debug-skip-loading-wait', '1', '--debug-no-settings', '--debug-no-records',
		'--audio', '0', '--debug-frames', '3', '--debug-capture', str(capture)]
	result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
		timeout=90, env=dict(os.environ, SDL_VIDEO_DRIVER='x11',
			SDL_AUDIO_DRIVER='dummy'))
	(reports / (installation.name + '.log')).write_text(result.stdout + result.stderr)
	passed = (result.returncode == 0 and capture.is_file()
		and capture.read_bytes()[:2] == b'BM')
	executable = (installation / 'motor.exe').read_bytes()
	print(f'{installation.name}: {"PASS" if passed else "BLOCKED"}', flush=True)

	return dict(edition=installation.name, passed=passed, exit=result.returncode,
		executable_size=len(executable),
		executable_sha256=hashlib.sha256(executable).hexdigest(),
		stderr=result.stderr)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--retail', type=Path, required=True)
	parser.add_argument('--demos', type=Path, required=True)
	parser.add_argument('--patch-components', type=Path,
		required=True)
	parser.add_argument('--patch31', type=Path,
		required=True)
	parser.add_argument('--reports', type=Path,
		default=Path('build/checks/compatibility/drop-in-startup'))
	args = parser.parse_args()
	binary = build_executable(args.build.resolve(), 'openmotor')
	reports = args.reports.resolve()
	reports.mkdir(parents=True, exist_ok=True)

	with tempfile.TemporaryDirectory(prefix='installations-', dir=reports) as temporary:
		work = Path(temporary)
		base = work / 'retail-12'
		shutil.copytree(args.retail.resolve(), base)
		installations = [base]

		for version in ('30', '31'):
			destination = work / ('retail-' + version)
			patched_installation(base, destination, args.patch_components.resolve(),
				args.patch31.resolve() if version == '31' else None)
			installations.append(destination)

		for demo in sorted(args.demos.resolve().iterdir()):
			if demo.is_dir():
				destination = work / demo.name
				shutil.copytree(demo, destination)
				installations.append(destination)

		results = [check_startup(binary, installation, work, reports)
			for installation in installations]

	(reports / 'startup.json').write_text(json.dumps(results, indent=2) + '\n')

	return 0 if all(result['passed'] for result in results) else 1


if __name__ == '__main__':
	raise SystemExit(main())
