add_library(mh_video_dispatch STATIC
	src/engine/video/player.c
	src/engine/video/layout.c)
target_include_directories(mh_video_dispatch PUBLIC src)
target_link_libraries(mh_video_dispatch PUBLIC mh_core)
mh_c89(mh_video_dispatch)

add_library(mh_video_decoding STATIC src/engine/video/decoder.c)
target_include_directories(mh_video_decoding PUBLIC src)
target_link_libraries(mh_video_decoding PUBLIC mh_error)
mh_c89(mh_video_decoding)

add_library(mh_video_playback STATIC src/engine/video/playback.c)
target_link_libraries(mh_video_playback
	PUBLIC
		mh_video_dispatch
		mh_video_decoding
	PRIVATE
		mh_video_sources
		mh_window_system
		mh_canvas
		mh_input
		mh_clock_source
		mh_audio_playback)
mh_c89(mh_video_playback)

add_library(mh_video_sources STATIC src/engine/video/source.c)
target_include_directories(mh_video_sources PUBLIC src)
target_link_libraries(mh_video_sources PUBLIC mh_error)
mh_c89(mh_video_sources)

add_library(mh_video_ffmpeg STATIC)
target_link_libraries(mh_video_ffmpeg PUBLIC mh_video_decoding)

if(MH_HAVE_FFMPEG AND MH_DECODE_MOVIES)
	target_sources(mh_video_ffmpeg PRIVATE
		src/backends/video/ffmpeg_decoder.c
		src/backends/video/ffmpeg_prepare.c
		src/backends/video/ffmpeg_frame.c)
	target_link_libraries(mh_video_ffmpeg PRIVATE
		mh_ffmpeg_input
		mh_audio_ffmpeg)
	if(UNIX)
		target_link_libraries(mh_video_ffmpeg PRIVATE m)
	endif()
else()
	target_sources(mh_video_ffmpeg PRIVATE src/backends/video/ffmpeg_stub.c)
endif()
mh_c89(mh_video_ffmpeg)

add_library(mh_video_ffmpeg_encoding STATIC)
target_link_libraries(mh_video_ffmpeg_encoding PUBLIC mh_video_decoding)

if(MH_HAVE_FFMPEG AND MH_ENCODE_MEDIA)
	target_sources(mh_video_ffmpeg_encoding PRIVATE
		src/backends/video/ffmpeg_encode.c
		src/backends/video/ffmpeg_encode_picture.c)
	target_link_libraries(mh_video_ffmpeg_encoding PRIVATE
		mh_audio_ffmpeg_encoding
		mh_ffmpeg_output)
else()
	target_sources(mh_video_ffmpeg_encoding PRIVATE
		src/backends/video/ffmpeg_encode_stub.c)
endif()
mh_c89(mh_video_ffmpeg_encoding)

add_library(mh_video_divi STATIC src/backends/video/divi_source.c)
target_link_libraries(mh_video_divi
	PUBLIC
		mh_video_sources
	PRIVATE
		mh_asset_loaders)
mh_c89(mh_video_divi)
