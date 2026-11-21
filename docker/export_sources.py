"""Retain dependency sources, build recipes and license notices."""

from pathlib import Path
import shutil
import subprocess
import tarfile


def export_sources(output, platform):
	# Retain the exact archives and recipes used for the linked libraries.
	with tarfile.open(output / 'dependency-sources.tar.gz', 'w:gz') as archive:
		archive.add('/downloads', arcname='archives')
		archive.add('/recipes', arcname='recipes')
		archive.add('docker/Dockerfile', arcname='recipes/Dockerfile')

		if platform != 'wasm':
			# Keep the application source and build inputs for relinking FFmpeg.
			for name in ('CMakeLists.txt', 'LICENSE.md', 'README.md', 'VERSION',
				'.clang-format', '.dockerignore', 'cmake', 'docker', 'scripts',
				'src', 'tests', 'tools'):
				archive.add(name, arcname='openmh/' + name)


def wasm_notices():
	sdk = Path(subprocess.check_output(
		['em-config', 'EMSCRIPTEN_ROOT'], text=True).strip())

	return {
		'emscripten.txt': sdk / 'LICENSE',
		'musl.txt': sdk / 'system/lib/libc/musl/COPYRIGHT',
		'compiler-rt.txt': sdk / 'system/lib/compiler-rt/LICENSE.TXT',
	}


def export_notices(output, platform):
	notices = {
		'OpenMH': 'LICENSE.md',
		'sdl.txt': '/deps/sdl/LICENSE.txt',
		'zlib.txt': '/deps/zlib/LICENSE',
		'unshield.txt': '/deps/unshield/LICENSE',
		'unshield-md5.txt': '/deps/unshield/lib/md5/md5c.c',
		'ffmpeg.txt': '/deps/ffmpeg/COPYING.LGPLv2.1',
		'ffmpeg-notes.txt': '/deps/ffmpeg/LICENSE.md',
		'opus.txt': '/deps/opus/COPYING',
		'vpx.txt': '/deps/vpx/LICENSE',
		'vpx-patents.txt': '/deps/vpx/PATENTS',
		'openssl.txt': '/deps/openssl/LICENSE.txt',
	}

	if platform == 'wasm':
		del notices['openssl.txt']
		notices.update(wasm_notices())

	if platform == 'windows':
		notices.update({
			'mingw-w64.txt': '/usr/share/doc/mingw-w64-common/copyright',
			'gcc-runtime.txt': '/usr/share/doc/gcc-mingw-w64-base/copyright',
			'gcc-gpl3.txt': '/usr/share/common-licenses/GPL-3',
		})

	sections = []

	for name, source in notices.items():
		text = Path(source).read_text()

		if name == 'unshield-md5.txt':
			text = text.partition('#include')[0]

		sections.append(name + '\n' + '=' * len(name) + '\n\n' + text)

	(output / 'licenses.txt').write_text('\n\n'.join(sections))

	shutil.copy2('docker/sources.txt', output / 'THIRD_PARTY.txt')
