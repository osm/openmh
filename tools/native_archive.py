"""Validate native release archives against the configured application targets."""

import json
from pathlib import Path
import re
import stat
import tarfile
import zipfile


APPLICATIONS = {"openmotor", "openmotorsv", "openmotorex"}


def executable_indexes(build):
	manifests = sorted(build.glob("executables-*.json"))

	if not manifests:
		raise ValueError(f"No configured executable index in {build}")

	for manifest in manifests:
		yield manifest, json.loads(manifest.read_text())


def configured_applications(build):
	return {Path(filename).name for _, entries in executable_indexes(build)
		for name, filename in entries.items() if name in APPLICATIONS}


def archive_entries(archive, windows):
	if windows:
		with zipfile.ZipFile(archive) as bundle:
			for member in bundle.infolist():
				kind = stat.S_IFMT(member.external_attr >> 16)
				is_file = not member.is_dir() and kind in (0, stat.S_IFREG)
				is_dir = member.is_dir() and kind in (0, stat.S_IFDIR)
				yield member.filename, is_dir, is_file, member.file_size
	else:
		with tarfile.open(archive, "r:gz") as bundle:
			for member in bundle:
				yield member.name, member.isdir(), member.isfile(), member.size


def check_archive(archive, applications, root, *, allow_libraries=True):
	windows = archive.suffix.lower() == ".zip"
	extension = ".zip" if windows else ".tar.gz"

	if archive.name != root + extension:
		raise ValueError(f"Expected the configured native archive {root + extension}")

	allowed = {name + (".exe" if windows else "") for name in APPLICATIONS}

	if not applications or applications - allowed:
		raise ValueError("Archive does not match the configured native applications")

	required = applications | {"licenses.txt"}
	library = re.compile(r"[\w.+-]+\.dll", re.IGNORECASE) if windows else re.compile(
		r"lib[\w.+-]+\.so(?:\.\d+)*")
	files = set()

	for path, is_dir, is_file, size in archive_entries(archive, windows):
		if is_dir and path in (root, root + "/"):
			continue

		prefix, separator, name = path.partition("/")

		if prefix != root or not separator or not is_file or not size:
			raise ValueError(f"Invalid archive entry: {path}")

		if name not in required and not (allow_libraries and library.fullmatch(name)):
			raise ValueError(f"Unexpected archive entry: {path}")

		name = name.lower() if windows else name

		if name in files:
			raise ValueError(f"Duplicate archive entry: {path}")

		files.add(name)

	if required - files:
		raise ValueError(f"Missing runtime files: {sorted(required - files)}")

	print(f"Archive: {len(files)} runtime files in {root}")
