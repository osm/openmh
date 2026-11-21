#!/usr/bin/env python3
"""Drive successive courses with shared score history, then reload their files."""

from tests.checks.support.build import build_executable
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--gamedir', type=Path, required=True)
	parser.add_argument('--build', type=Path, default=Path('build/linux'))
	parser.add_argument('--forward', action='store_true', help='use courses 1/2 for original assets')
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix='mh-cross-track-records-') as directory:
		command = [str(build_executable(args.build, 'mh-cross-track-records')), str(args.gamedir), directory]

		if args.forward:
			command.append('forward')

		result = subprocess.run(command, capture_output=True, text=True, timeout=300)
		assert result.returncode == 0, result.stdout + result.stderr
		rows = re.findall(r'Course (\d+) (\S+): finished (\d), lap (\d+), total (\d+), retained (\d+)', result.stdout)
		assert len(rows) == 3, result.stdout
		first, aborted, second = rows
		assert (first[0], aborted[0], second[0]) == ('1', '2' if args.forward else '9', '2' if args.forward else '9')
		assert first[1] != second[1] and aborted[1] == second[1]
		assert (first[2], aborted[2], second[2]) == ('1', '0', '1')
		assert aborted[3:] == ('1000000', '0', first[4])
		expected = {pattern.format(track=row[1]) for row in (first, second) for pattern in
			('{track}.CL', '{track}01.CR', 'BLMM00{track}.MHI',
				'BRMM01{track}.MHI', 'SRMM01{track}.MHI')}
		assert {p.name for p in Path(directory).iterdir()} == expected
		print(result.stdout, end='')
		print('Shared-session finish -> new-course abort -> finish: ten separate record files; reload matches every saved payload byte')


if __name__ == '__main__':
	main()
