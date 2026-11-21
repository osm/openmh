set(MH_HAVE_FFMPEG FALSE)

if(NOT MH_USE_FFMPEG OR (NOT MH_DECODE_OPUS AND NOT MH_DECODE_MOVIES AND
	NOT MH_ENCODE_MEDIA))
	message(STATUS "FFmpeg media support disabled by build selection")
	return()
endif()
# SDK headers stay private; all callers use the project's service interfaces.
find_path(MH_FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h)

foreach(component avformat avcodec avutil swresample swscale)
	find_library(MH_FFMPEG_${component}_LIBRARY NAMES ${component})
	mark_as_advanced(MH_FFMPEG_${component}_LIBRARY)
endforeach()
mark_as_advanced(MH_FFMPEG_INCLUDE_DIR)
set(MH_HAVE_FFMPEG FALSE)
set(mh_ffmpeg_version_supported FALSE)

if(EXISTS "${MH_FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h")
	file(STRINGS "${MH_FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h"
		mh_ffmpeg_version_line REGEX
			"^#define LIBAVCODEC_VERSION_MAJOR +[0-9]+")
	string(REGEX MATCH "[0-9]+" mh_ffmpeg_codec_major
		"${mh_ffmpeg_version_line}")

	if(mh_ffmpeg_codec_major GREATER_EQUAL 60)
		set(mh_ffmpeg_version_supported TRUE)
	endif()
endif()

if(MH_FFMPEG_INCLUDE_DIR AND MH_FFMPEG_avformat_LIBRARY AND
	MH_FFMPEG_avcodec_LIBRARY AND MH_FFMPEG_avutil_LIBRARY AND
	MH_FFMPEG_swresample_LIBRARY AND MH_FFMPEG_swscale_LIBRARY AND
	mh_ffmpeg_version_supported)

	add_library(mh_ffmpeg_sdk INTERFACE)
	target_include_directories(mh_ffmpeg_sdk SYSTEM INTERFACE
		"${MH_FFMPEG_INCLUDE_DIR}")

	if(NOT EMSCRIPTEN AND MH_FFMPEG_avformat_LIBRARY MATCHES "\\.a$" AND
		NOT MH_FFMPEG_avformat_LIBRARY MATCHES "\\.dll\\.a$")
		# Static codecs also need the external libraries enabled by this SDK.
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(MH_FFMPEG_PC REQUIRED
			libavformat libavcodec libswscale libswresample libavutil)
		target_link_directories(mh_ffmpeg_sdk INTERFACE
			${MH_FFMPEG_PC_STATIC_LIBRARY_DIRS})
		target_link_libraries(mh_ffmpeg_sdk INTERFACE
			${MH_FFMPEG_PC_STATIC_LIBRARIES})
		target_link_options(mh_ffmpeg_sdk INTERFACE
			${MH_FFMPEG_PC_STATIC_LDFLAGS_OTHER})
	else()
		foreach(component avformat avcodec swscale swresample avutil)
			target_link_libraries(mh_ffmpeg_sdk INTERFACE
				"${MH_FFMPEG_${component}_LIBRARY}")
		endforeach()
	endif()

	if(EMSCRIPTEN)
		# Static FFmpeg builds may include these optional media encoders.
		# Decoder-only builds do not need either library.
		foreach(codec opus vpx)
			find_library(MH_FFMPEG_${codec}_LIBRARY NAMES ${codec})
			mark_as_advanced(MH_FFMPEG_${codec}_LIBRARY)

			if(MH_FFMPEG_${codec}_LIBRARY)
				target_link_libraries(mh_ffmpeg_sdk INTERFACE
					"${MH_FFMPEG_${codec}_LIBRARY}")
			endif()
		endforeach()
	endif()

	add_library(mh_ffmpeg_input STATIC src/backends/media/ffmpeg_input.c)
	target_link_libraries(mh_ffmpeg_input
		PUBLIC
			mh_ffmpeg_sdk
		PRIVATE
			mh_resource_source)
	mh_c89(mh_ffmpeg_input)

	add_library(mh_ffmpeg_output STATIC src/backends/media/ffmpeg_output.c)
	target_link_libraries(mh_ffmpeg_output
		PUBLIC
			mh_ffmpeg_sdk
		PRIVATE
			mh_resource_source)
	mh_c89(mh_ffmpeg_output)
	set(MH_HAVE_FFMPEG TRUE)
	message(STATUS "FFmpeg media libraries: ${MH_FFMPEG_avformat_LIBRARY}")
else()
	message(STATUS
		"FFmpeg 6+ development files not found; compact media disabled")
endif()
