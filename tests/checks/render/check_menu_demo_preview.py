#!/usr/bin/env python3
"""Exercise installed demo catalog restoration in the live frontend adapter.

Fixture roots link read-only game resources and contain private filename-only
demo fixtures. These cases never launch or delete a recording. Captures must
preserve both the input settings and all catalog entries.
"""

from tests.checks.support.build import build_executable
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from tests.checks.support.images import bmp_rgb


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build',type=Path,default=Path('build/linux'))
	parser.add_argument('--gamedir',type=Path,required=True)
	parser.add_argument('--report',type=Path)
	parser.add_argument('--compare',type=Path)
	args = parser.parse_args()
	results = []

	with tempfile.TemporaryDirectory(prefix='mh-demo-menu-') as temporary:
		root = Path(temporary)
		env = dict(os.environ)
		cases = [(['z.MDE','JeePen.mde','Race.one.MDE'],'jEePeN','JeePen'),
			(['z.MDE','JeePen.mde','Race.one.MDE'],'race.ONE','Race.one'),
			(['One.MDE'],'absent','One'),([], 'old recording',''),
			(['x'*63+'.MDE'],'x'*63,'x'*63)]

		for i,(names,saved,wanted) in enumerate(cases):
			assets = root/str(i)
			assets.mkdir()
			demos = assets/'Demos'
			demos.mkdir()

			for path in args.gamedir.resolve().iterdir():
				if path.name.lower() != 'demos':
					(assets/path.name).symlink_to(path,target_is_directory=path.is_dir())

			for name in names:
				(demos/name).write_bytes(b'filename-only fixture\n')

			(demos/'ignored.txt').write_bytes(b'not a demo\n')
			snapshot = {p.name:p.read_bytes() for p in demos.iterdir()}
			cfg = root/f'{i}.cfg'
			cfg.write_text('CarHorn Honker1\nGameType GhostMode\nDemoFileName '+saved+'\n')
			before = cfg.read_bytes()
			stamp = cfg.stat().st_mtime_ns
			bmp,raw = root/f'{i}.bmp',root/f'{i}.idx'
			output = subprocess.run([str(build_executable(args.build, 'openmotor')), '--debug-no-intro', '--debug-skip-loading-wait', '1','--assets',str(assets),
				'--debug-settings',str(cfg),'--debug-page','single','--debug-selection','4','--audio', '0',
				'--debug-frames','120','--debug-capture',str(bmp),'--debug-indexed',str(raw), '--debug-window-titles', '1'],
				env=env,capture_output=True,text=True,timeout=60)
			assert output.returncode == 0,output
			row = next(line for line in output.stdout.splitlines() if line.startswith('Demo catalog:'))
			assert f'count {len(names)},' in row and row.endswith(', name '+wanted),row
			assert cfg.read_bytes() == before and cfg.stat().st_mtime_ns == stamp
			assert snapshot == {p.name:p.read_bytes() for p in demos.iterdir()}
			pixels = raw.read_bytes()
			rgb = bmp_rgb(bmp)
			assert len(pixels) == 256768
			assert rgb == b''.join(pixels[n*3:n*3+3] for n in pixels[768:])

			# Enumeration order is OS-owned; selected index may differ, but
			# the saved name and rendered pixels must agree across builds.
			results.append(dict(name=wanted,count=len(names),rgb_sha256=hashlib.sha256(rgb).hexdigest()))

		assert not (root/'user').exists(), 'capture created implicit per-user files'

	if args.compare:
		assert results == json.loads(args.compare.read_text()),'demo panel differs across builds/backends'

	if args.report:
		args.report.write_text(json.dumps(results,indent=2)+'\n')

	print(f'{len(results)} frontend demo panels passed: saved basenames, extension removal, empty catalog and read-only preservation')


if __name__ == '__main__':
	main()
