"""Export the versioned browser runtime directory and its archive."""

import json
import shutil


def export_runtime(directory, output):
	runtime = directory / 'browser-runtime'
	manifest = json.loads((runtime / 'runtime.json').read_text())
	shutil.copytree(runtime, output / 'browser-runtime')
	shutil.copy2(directory / f'openmh-{manifest["version"]}-wasm.tar.gz', output)
