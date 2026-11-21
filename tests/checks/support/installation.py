"""Prepare private game directories with shared read-only assets."""


def prepare_view(destination, source, replacements=None):
	"""Link read-only assets into a private installation with optional fixtures."""

	replacements = replacements or {}
	replaced = {name.lower() for name in replacements}
	destination.mkdir()

	for entry in source.resolve().iterdir():
		if entry.name.lower() not in replaced | {
			'userdata', 'original', 'openmotor', 'openmotorsv', 'openmotorex'}:
			(destination / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())

	for name, path in replacements.items():
		(destination / name).symlink_to(path.resolve(), target_is_directory=True)
