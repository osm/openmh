#!/usr/bin/env python3
"""Verify the patched hardware particle/glow inputs against the disc assets."""

import argparse
import hashlib
from pathlib import Path


def files(root):
	return {str(p.relative_to(root)).lower(): p for p in root.rglob('*')
		if p.is_file() and p.suffix.lower() == '.iff'}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--disc', type=Path, required=True)
	parser.add_argument('--patched', type=Path, required=True)
	args = parser.parse_args()
	disc, patched = files(args.disc), files(args.patched)
	names = ['data/halofix.iff', 'data/hwhex.iff']
	names += [f'data/d3d/{group}/{stem}{n:02d}.iff'
		for group, stem, count in [('smoke', 'smoke', 9),
			('grass', 'gras', 9), ('sparks', 'spark', 6)]
		for n in range(1, count+1)]
	digest = hashlib.sha256()

	for name in sorted(names):
		before, after = disc[name].read_bytes(), patched[name].read_bytes()
		assert before == after, ('changed effect asset', name)
		digest.update(name.encode('ascii') + b'\0')
		digest.update(hashlib.sha256(after).digest())

	print(f'{len(names)} hardware particle/glow assets are byte-identical to the disc')
	print(f'Sorted path-NUL-SHA256 manifest: {digest.hexdigest()}')


if __name__ == '__main__':
	main()
