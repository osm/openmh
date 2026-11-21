#!/usr/bin/env python3
"""Identify every prepared edition from content, without installation manifests."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from tests.checks.support.build import build_executable


# Expected identities are independent of the C detection table.
CASES = (
	('retail/original', 'retail-europe', 1, 3),
	('retail/patched', 'retail-europe', 2, 3),
	('demos/motordemo2_3dfx_intro', 'motordemo2_3dfx_intro', 0, 4),
	('demos/motordemo2_soft', 'motordemo2_soft', 0, 0),
	('demos/motordemo_3dfx', 'motordemo_3dfx', 0, 0),
	('demos/motordemo_3dfx_intro', 'motordemo_3dfx_intro', 0, 2),
	('demos/motordemo_mplayer', 'motordemo_mplayer', 0, 0),
	('demos/motordemo_soft', 'motordemo_soft', 0, 0),
	('demos/motordemo_soft_intro', 'motordemo_soft_intro', 0, 2),
)


def check(build, root, edition, revision, intro):
	subprocess.run([str(build_executable(build, 'test-content-probe')), str(root),
		edition, str(revision), str(intro)], check=True, timeout=30)
	subprocess.run([str(build_executable(build, 'test-content-startup')), str(root),
		edition], check=True, timeout=30)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build', type=Path, required=True)
	parser.add_argument('--gamedir', type=Path, required=True)
	args = parser.parse_args()
	build = args.build.resolve()

	with tempfile.TemporaryDirectory(prefix='mh-content-probe-') as temporary:
		work = Path(temporary)

		for index, (directory, edition, revision, intro) in enumerate(CASES):
			source = args.gamedir.resolve() / directory
			root = work / f'renamed installation {index}'
			root.mkdir()

			# No dependence on installer, original directory name, or metadata.
			shutil.copy2(source / 'motor.exe', root / 'MOTOR.EXE')

			if (source / 'Movies').exists():
				(root / 'movies').symlink_to(source / 'Movies', target_is_directory=True)

			if directory.startswith('retail/'):
				(root / 'data').mkdir()
				shutil.copy2(source / 'Data/Material.mat', root / 'data/material.MAT')

			check(build, root, edition, revision, intro)

			# Stale extraction metadata is not read or trusted.
			(root / 'manifest.txt').write_text('deliberately invalid metadata\n')
			(root / 'Game').mkdir()
			(root / 'Game/car01.car').write_text('custom car bytes\n')
			(root / 'Game/Track1.trk').write_text('custom track bytes\n')
			check(build, root, edition, revision, intro)

			if (root / 'movies').exists():
				(root / 'movies').unlink()
				without_intro = edition.removesuffix('_intro') if edition in (
					'motordemo_3dfx_intro', 'motordemo_soft_intro') else edition
				check(build, root, without_intro, revision, 0)

			if directory.startswith('retail/'):
				table = root / 'data/material.MAT'
				original = table.read_bytes()
				table.write_bytes(b'?' + original[1:])
				check(build, root, edition, 0, 0)
				table.unlink()
				check(build, root, edition, 0, 0)

			# Same-size corruption must not be guessed as retail or a demo.
			executable = root / 'MOTOR.EXE'
			original = executable.read_bytes()
			executable.write_bytes(b'?' + original[1:])
			result = subprocess.run([str(build_executable(build, 'test-content-probe')),
				str(root), edition, str(revision), str(intro)],
				capture_output=True, text=True, timeout=30)
			assert result.returncode != 0 and 'unrecognized' in result.stderr, result.stderr

	print('Nine editions: case/relocation, optional intros, edited assets, ignored manifests, '
		'patch markers and unknown-executable rejection passed.')


if __name__ == '__main__':
	main()
