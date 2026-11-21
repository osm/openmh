"""Resolve CMake executable targets for development checks."""

import json
import os
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=None)
def _executables(build, configuration):
	if configuration:
		manifests = [build / f"executables-{configuration}.json"]
	else:
		manifests = sorted(build.glob("executables-*.json"))

	if not manifests or not manifests[0].is_file():
		raise ValueError(f"Configure {build} with CMake before running checks")

	if len(manifests) != 1:
		raise ValueError("Set MH_BUILD_CONFIG to select a build configuration")

	return json.loads(manifests[0].read_text())


def build_executable(build, name):
	"""Return the configured target path, including tools/ and tests/ outputs."""

	build = Path(build).resolve()
	targets = _executables(build, os.environ.get("MH_BUILD_CONFIG", ""))

	if name not in targets:
		raise ValueError(
			f"{name} is not configured in {build}; enable its application, "
			"BUILD_TESTING or MH_BUILD_TOOLS option")

	return Path(targets[name])
