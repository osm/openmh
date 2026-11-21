"""Export Linux executables and check their system library imports."""

import subprocess


SYSTEM_LIBRARIES = {
	'libc.so.6', 'libdl.so.2', 'libgcc_s.so.1', 'libm.so.6',
	'libpthread.so.0', 'librt.so.1',
}


def export_runtime(directory, output):
	subprocess.run([
		'cmake', '--install', str(directory), '--strip', '--prefix', str(output),
		'--component', 'Runtime',
	], check=True)
	binary = output / 'bin'
	binary.mkdir()

	for name in ('openmotor', 'openmotorsv', 'openmotorex'):
		path = output / name

		if path.is_file():
			imports = subprocess.check_output(
				['patchelf', '--print-needed', str(path)], text=True)
			unexpected = set(imports.splitlines()) - SYSTEM_LIBRARIES

			if unexpected:
				raise ValueError(f'{name} requires additional libraries: {sorted(unexpected)}')

			subprocess.run([str(path), '--help'], check=True)
			path.rename(binary / name)
