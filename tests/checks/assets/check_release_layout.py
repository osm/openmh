#!/usr/bin/env python3
"""Check executable ownership and the explicit runtime archive contents."""

import argparse
from pathlib import Path
import sys

if not __package__:
	sys.path.insert(0, str(Path(__file__).resolve().parents[3]))


def check_outputs(build, applications_only):
	from tools.native_archive import APPLICATIONS, executable_indexes

	for manifest, entries in executable_indexes(build):
		for name, filename in entries.items():
			relative = Path(filename).relative_to(build)

			if name in APPLICATIONS:
				continue

			if applications_only:
				raise ValueError(f"Unexpected development target: {name}")

			if relative.parts[0] not in {"tools", "tests"}:
				raise ValueError(f"Development executable outside its directory: {relative}")

		print(f"{manifest.name}: {len(entries)} executable targets checked")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, required=True)
	parser.add_argument("--applications-only", action="store_true")
	parser.add_argument("--archive", type=Path)
	args = parser.parse_args()
	build = args.build.resolve()
	check_outputs(build, args.applications_only)

	if args.archive:
		from tools.native_archive import check_archive, configured_applications
		from tools.release import package_name

		check_archive(args.archive, configured_applications(build), package_name(build))


if __name__ == "__main__":
	main()
