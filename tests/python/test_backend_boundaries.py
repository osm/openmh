#!/usr/bin/env python3
"""Check layer dependencies, not the implementation of individual features.

These are lightweight source checks, not a C parser or runtime verification.
Only architectural boundaries and public construction APIs are named here;
helper names, member assignments and the presence of old files are not tested.
"""

from pathlib import Path
from fnmatch import fnmatchcase
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src"
APPLICATIONS = ("client", "server", "extract")
PORTABLE = ("engine", "interfaces", "client", "extract/cli", "extract/output",
	"extract/prepare", "extract/source", "server/commands",
	"server/config", "server/courses", "server/session", "server/cli",
	"server/console")
DIAGNOSTIC_ADAPTERS = (
	"client/diagnostics/asset_*", "client/diagnostics/input.*",
	"client/diagnostics/media_*", "client/diagnostics/render_*",
	"client/diagnostics/runtime_*", "client/diagnostics/storage_*",
	"client/diagnostics/model_viewer.*",
)
COMPOSITION = DIAGNOSTIC_ADAPTERS + (
	"client/hosts/*", "client/diagnostics/session.*",
	"client/diagnostics/session_*", "server/cli/*", "extract/*",
)
RENDER_PROFILES = ("direct3d_", "glide_", "openmh_", "powervr_", "software_")
RENDER_COMPOSITION = ("atmosphere_", "effect_", "mesh", "scene")

# Shared geometry and data contracts also used by scene/effect composition.
RENDER_SHARED = ("effect_quad", "scene_effect", "scene_face", "scene_face_internal")
LITERALS = re.compile(
	r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
	re.DOTALL,
)
INCLUDES = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.MULTILINE)
SDK_SYMBOL = re.compile(
	r'\b(?:SDL_\w+|SDLK_\w+|Uint(?:8|16|32|64)|Sint(?:8|16|32|64)'
	r'|AVCodec\w*|AVFormat\w*|AVFrame|AVPacket|AVIOContext|AVStream|AVDictionary'
	r'|AVRational|AVChannelLayout|AVAudioFifo|SwrContext|SwsContext'
	r'|GL_\w+|GL[a-z]\w*|gl[A-Z]\w*'
	r'|ALC?_\w+|ALC?[a-z]\w*|alc?[A-Z]\w*'
	r'|EM_\w+|EMSCRIPTEN_\w+|Emscripten\w+|emscripten_\w+)\b')
NATIVE_HEADERS = {"unistd.h", "dirent.h", "fcntl.h", "windows.h",
	"winsock2.h", "ws2tcpip.h", "process.h"}


# Host composition may select concrete providers through these SDK-free APIs.
# This is an explicit export list, not permission to include private headers.
BACKEND_API = {
	"backends/retro/provider.h",
	"backends/catalog/provider.h",
	"backends/catalog/adp_provider.h",
	"backends/material/provider.h",
	"backends/audio/esd_provider.h",
	"backends/audio/chf_provider.h",
	"backends/audio/provider.h",
	"backends/navigation/provider.h",
	"backends/animation/lightwave_provider.h",
	"backends/geometry/lob_provider.h",
	"backends/image/remap_provider.h",
	"backends/geometry/myo_provider.h",
	"backends/geometry/myw_provider.h",
	"backends/collision/col_provider.h",
	"backends/collision/provider.h",
	"backends/font/fnt_provider.h",
	"backends/font/provider.h",
	"backends/sprite/spr_provider.h",
	"backends/archive/unshield_provider.h",
	"backends/archive/mscab_provider.h",
	"backends/audio/sdl_provider.h",
	"backends/audio/openal_provider.h",
	"backends/runtime/emscripten_provider.h",
	"backends/audio/wave_provider.h",
	"backends/audio/ffmpeg_provider.h",
	"backends/audio/ffmpeg_encode.h",
	"backends/clock/system_provider.h",
	"backends/input/sdl_provider.h",
	"backends/image/tga_provider.h",
	"backends/image/png_provider.h",
	"backends/image/act_provider.h",
	"backends/ui/dta_provider.h",
	"backends/image/ilbm_provider.h",
	"backends/image/provider.h",
	"backends/image/pdi_provider.h",
	"backends/network/udp_transport_provider.h",
	"backends/network/udp_provider.h",
	"backends/network/browser_provider.h",
	"backends/network/webrtc_provider.h",
	"backends/network/combined_provider.h",
	"backends/network/signaling_provider.h",
	"backends/network/posix_provider.h",
	"backends/network/windows_provider.h",
	"backends/render/sdl_provider.h",
	"backends/render/sdl_gpu_provider.h",
	"backends/render/sdl_canvas_provider.h",
	"backends/render/software_provider.h",
	"backends/render/webgl_provider.h",
	"backends/storage/filesystem_provider.h",
	"backends/storage/indexeddb_provider.h",
	"backends/storage/zip_provider.h",
	"backends/storage/filesystem_staging.h",
	"backends/storage/filesystem_file_read.h",
	"backends/storage/filesystem_tree.h",
	"backends/storage/memory_resource.h",
	"backends/storage/memory_user_data.h",
	"backends/video/ffmpeg_provider.h",
	"backends/video/ffmpeg_encode.h",
	"backends/video/divi_provider.h",
	"backends/window/sdl_provider.h",
}


def native_header(name):
	return (name.startswith(("SDL", "enet/", "sys/", "libav", "libsw",
		"GL/", "GLES/", "GLES2/", "GLES3/", "AL/",
		"OpenAL/", "emscripten/"))
		or name in NATIVE_HEADERS or name == "emscripten.h")


def source_files(directory):
	return sorted(path for path in directory.rglob("*")
		if path.suffix in (".c", ".h"))


def includes(path):
	# Ignore commented-out directives, while keeping quoted header names.
	text = LITERALS.sub(lambda match: "" if match[0].startswith(("/*", "//"))
		else match[0], path.read_text())

	for marker, name in INCLUDES.findall(text):
		dependency = None

		if marker == '"':
			for candidate in (path.parent / name, SOURCE / name):
				if candidate.is_file():
					dependency = candidate.resolve()

					if dependency.is_relative_to(SOURCE):
						name = dependency.relative_to(SOURCE).as_posix()

					break

		yield name, dependency


def may_select_backend(path, include):
	return include in BACKEND_API and path.suffix == ".c" and any(
		fnmatchcase(path.relative_to(SOURCE).as_posix(), group)
		for group in COMPOSITION)


class BackendBoundaries(unittest.TestCase):
	def test_browser_sdk_detection(self):
		for header in ("GLES3/gl3.h", "GLES2/gl2.h", "GL/gl.h", "AL/al.h",
			"AL/alc.h", "OpenAL/al.h", "emscripten.h",
			"emscripten/html5.h"):
			with self.subTest(header=header):
				self.assertTrue(native_header(header))

		for symbol in ("GLuint", "GLenum", "GL_TEXTURE_2D", "glBindBuffer",
			"ALuint", "ALCdevice", "AL_FORMAT_STEREO16",
			"alSourcePlay", "alcOpenDevice", "EM_TRUE",
			"EMSCRIPTEN_WEBGL_CONTEXT_HANDLE",
			"EmscriptenWebGLContextAttributes", "emscripten_get_now"):
			with self.subTest(symbol=symbol):
				self.assertRegex(symbol, SDK_SYMBOL)

		for symbol in ("MH_GPU_TRIANGLES", "mh_webgl_renderer", "mh_openal_audio_open"):
			self.assertNotRegex(symbol, SDK_SYMBOL)

	def test_portable_sources_and_headers_are_sdk_free(self):
		pending = [path for directory in PORTABLE
			for path in source_files(SOURCE / directory)]
		self.assertTrue(pending, "No portable sources found")
		seen = set()
		failures = []

		while pending:
			path = pending.pop()

			if path in seen:
				continue

			seen.add(path)
			label = path.relative_to(ROOT)

			for include, dependency in includes(path):
				if native_header(include):
					failures.append(f"{label}: native include {include}")
				elif include.startswith("backends/") and not may_select_backend(path, include):
					# Public construction headers may reference other public headers.
					if (path.relative_to(SOURCE).as_posix() not in BACKEND_API
						or include not in BACKEND_API):
						failures.append(f"{label}: private backend dependency {include}")

				if dependency is not None:
					pending.append(dependency)

			for symbol in sorted(set(SDK_SYMBOL.findall(LITERALS.sub("", path.read_text())))):
				failures.append(f"{label}: backend type or operation {symbol}")

		self.assertFalse(failures, "\n".join(failures))

	def test_shared_layers_and_applications_are_independent(self):
		for owner in ("engine", "interfaces", "backends", *APPLICATIONS):
			for path in source_files(SOURCE / owner):
				for include, _ in includes(path):
					target = include.split("/", 1)[0]

					with self.subTest(source=str(path.relative_to(ROOT)), include=include):
						if target in APPLICATIONS:
							self.assertEqual(target, owner,
								"Shared code and other applications must not depend on an application")

						if owner in ("engine", "interfaces"):
							self.assertNotEqual(target, "backends",
								"Portable layers must use backend-independent contracts")

	def test_public_backend_headers_hide_private_implementation(self):
		# This is an export allowlist, not a requirement to retain every backend.
		for name in sorted(BACKEND_API):
			path = SOURCE / name

			if not path.is_file():
				continue

			for include, dependency in includes(path):
				if dependency is not None:
					self.assertTrue(include.startswith(("interfaces/", "engine/"))
						or include in BACKEND_API,
						f"{name}: private dependency {include}")
				else:
					self.assertFalse(native_header(include),
						f"{name}: native dependency {include}")

			self.assertNotRegex(LITERALS.sub("", path.read_text()), SDK_SYMBOL, name)

	def test_render_profiles_are_independent(self):
		for path in source_files(SOURCE / "engine/render"):
			owner = next((prefix for prefix in RENDER_PROFILES
				if path.name.startswith(prefix)), None)
			shared = owner is None and (path.stem in RENDER_SHARED or
				not path.name.startswith(RENDER_COMPOSITION))

			for include, _ in includes(path):
				if not include.startswith("engine/render/"):
					continue

				dependency = next((prefix for prefix in RENDER_PROFILES
					if Path(include).name.startswith(prefix)), None)

				if dependency is not None and (owner is not None or shared):
					self.assertEqual(owner, dependency,
						f"{path}: depends on profile through {include}")

		for path in source_files(SOURCE / "interfaces/render"):
			for include, _ in includes(path):
				self.assertFalse(include.startswith("engine/render/")
					and Path(include).name.startswith(RENDER_PROFILES),
					f"{path}: rendering contract depends on profile {include}")

	def test_backend_render_profiles_are_independent(self):
		def owner(name):
			for service in ("sdl_gpu_", "webgl_", "shader_"):
				if name.startswith(service):
					name = name[len(service):]
					break

			return next((prefix for prefix in RENDER_PROFILES
				if name.startswith(prefix)), None)

		shader = re.compile(
			r"\bMH_GPU_(GLIDE|DIRECT3D|POWERVR|OPENMH)_"
			r"(?:VERTEX|GLENZ_VERTEX|FRAGMENT)\b")

		for path in sorted((SOURCE / "backends/render").iterdir()):
			if path.suffix not in (".c", ".h", ".vert", ".frag", ".glsl"):
				continue

			profile = owner(path.name)

			if profile is None:
				continue

			for include, _ in includes(path):
				if not include.startswith(("engine/render/", "backends/render/")):
					continue

				dependency = owner(Path(include).name)

				if dependency is not None:
					self.assertEqual(profile, dependency,
						f"{path}: depends on profile through {include}")

			for dependency in shader.findall(LITERALS.sub("", path.read_text())):
				self.assertEqual(profile, dependency.lower() + "_",
					f"{path}: selects another profile's shader")

	def test_audio_profiles_are_independent(self):
		profiles = ("software", "ds3d", "eax", "a3d12", "a3d20")

		def profile(name):
			prefix = Path(name).stem.split("_", 1)[0]

			return prefix if prefix in profiles else None

		for path in source_files(SOURCE / "engine/audio"):
			owner = profile(path.name)

			if owner is None and not path.name.startswith("spatial_"):
				continue

			for include, _ in includes(path):
				if not include.startswith("engine/audio/"):
					continue

				dependency = profile(include)

				if dependency is not None:
					self.assertEqual(owner, dependency,
						f"{path}: depends on profile through {include}")

	def test_network_protocols_are_independent(self):
		entries = {"engine/network/legacy.h", "engine/network/openmh.h",
			"backends/network/udp_legacy.h",
			"backends/network/udp_openmh.h"}
		registries = {"engine/network/protocol_profile.c",
			"backends/network/udp_protocol.c"}

		for directory in ("engine/network", "backends/network"):
			for path in source_files(SOURCE / directory):
				owner = path.relative_to(SOURCE).as_posix()

				for include, _ in includes(path):
					if include in entries and owner not in registries:
						self.assertEqual(path.stem, Path(include).stem,
							f"{owner}: selects concrete protocol {include}")

	def test_hosts_do_not_depend_on_diagnostic_adapters(self):
		for path in source_files(SOURCE / "client/hosts"):
			for include, _ in includes(path):
				self.assertFalse(any(fnmatchcase(include, adapter)
					for adapter in DIAGNOSTIC_ADAPTERS),
					f"{path}: host depends on diagnostic adapter {include}")

	def test_common_host_does_not_select_sdl_services(self):
		for path in source_files(SOURCE / "client/hosts"):
			if path.name.startswith(("sdl_", "browser_")):
				continue

			for include, _ in includes(path):
				self.assertFalse(include.startswith("client/hosts/sdl_")
					or (include.startswith("backends/")
						and Path(include).name.startswith("sdl_")),
					f"{path}: shared host selects SDL services")

		for path in (SOURCE / "client/hosts").glob("browser_*"):
			if path.suffix not in (".c", ".h"):
				continue

			for include, _ in includes(path):
				self.assertNotEqual(include, "client/hosts/sdl_application.h",
					f"{path}: browser host depends on native application setup")

	def test_portable_core_does_not_perform_file_io(self):
		native_io = re.compile(
			r"\b(?:fopen|freopen|fread|fwrite|fseek|ftell|"
			r"fclose|tmpfile|remove|rename)\s*\(")

		for path in source_files(SOURCE / "engine/core"):
			self.assertNotRegex(LITERALS.sub("", path.read_text()), native_io,
				f"{path}: portable core performs native file I/O")


if __name__ == "__main__":
	unittest.main()
