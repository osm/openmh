"""Export Windows executables and check their system DLL imports."""

from pathlib import Path
import re
import subprocess


SYSTEM_LIBRARIES = frozenset((
	'advapi32 bcrypt comctl32 comdlg32 crypt32 dbghelp gdi32 imm32 '
	'iphlpapi kernel32 mswsock msvcrt ntdll ole32 oleaut32 secur32 '
	'setupapi shell32 ucrtbase user32 userenv version winmm ws2_32'
).split())


def is_system_library(name):
	return (Path(name).stem in SYSTEM_LIBRARIES or
		name.startswith(('api-ms-win-', 'ext-ms-')))


def export_runtime(directory, output):
	binary = output / 'bin'
	binary.mkdir()

	for name in ('openmotor.exe', 'openmotorsv.exe', 'openmotorex.exe'):
		path = directory / name

		if path.is_file():
			imports = subprocess.check_output(
				['x86_64-w64-mingw32-objdump', '-p', str(path)], text=True)

			for library in re.findall(r'DLL Name:\s+(\S+)', imports):
				if not is_system_library(library.lower()):
					raise ValueError(f'{name} requires additional DLL: {library}')

			subprocess.run(['x86_64-w64-mingw32-strip', '-o',
				str(binary / name), str(path)], check=True)
