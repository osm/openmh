if(MH_HAVE_FFMPEG)
	add_executable(mh-movie-decode-check tests/integration/video_decode.c)
	target_link_libraries(mh-movie-decode-check PRIVATE
		mh_video_ffmpeg
		mh_resource_filesystem)
	mh_c89(mh-movie-decode-check)
endif()

if(BUILD_TESTING AND TARGET mh_diagnostic_video)
	add_executable(mh-content-opening-check
		tests/integration/content_opening.c)
	target_link_libraries(mh-content-opening-check PRIVATE
		mh_opening
		mh_content
		mh_host_sdl_video
		mh_resource_filesystem
		mh_window_sdl
		mh_clock_system)
	mh_c89(mh-content-opening-check)

	add_executable(mh-movie-events-check tests/integration/video_events.c)
	target_link_libraries(mh-movie-events-check PRIVATE
		mh_diagnostic_video
		mh_resource_memory
		SDL3::SDL3)
	target_include_directories(mh-movie-events-check SYSTEM PRIVATE
		"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
	mh_c89(mh-movie-events-check)
endif()

if(BUILD_TESTING)
	add_executable(test-video-source tests/unit/test_video_source.c)
	target_link_libraries(test-video-source PRIVATE
		mh_video_divi
		mh_resource_memory)
	mh_c89(test-video-source)
	add_test(NAME video-source-provider COMMAND test-video-source)

	add_executable(test-movie-provider tests/unit/test_video_provider.c)
	target_link_libraries(test-movie-provider PRIVATE
		mh_video_dispatch
		mh_asset_loaders
		mh_resource_memory)
	mh_c89(test-movie-provider)
	add_test(NAME video-provider COMMAND test-movie-provider)
endif()

if(BUILD_TESTING)
	add_executable(test-opening-provider tests/unit/test_opening_provider.c)
	target_link_libraries(test-opening-provider PRIVATE
		mh_opening
		mh_video_dispatch
		mh_window_system
		mh_resource_memory
		mh_content)
	mh_c89(test-opening-provider)
	add_test(NAME opening-provider COMMAND test-opening-provider)
endif()

if(BUILD_TESTING AND MH_HAVE_FFMPEG)
	add_executable(mh-movie-services-check
		tests/integration/video_services.c)
	target_link_libraries(mh-movie-services-check PRIVATE
		mh_video_playback
		mh_video_divi
		mh_video_ffmpeg
		mh_resource_filesystem
		mh_resource_memory
		mh_window_system
		mh_canvas
		mh_input
		mh_clock_source)
	mh_c89(mh-movie-services-check)
endif()

if(MH_HAVE_FFMPEG AND MH_DECODE_OPUS AND MH_DECODE_MOVIES AND
	MH_ENCODE_MEDIA AND TARGET mh_resource_filesystem)

	add_executable(mh-opus-audio-check tests/integration/opus_audio.c)
	target_link_libraries(mh-opus-audio-check PRIVATE
		mh_audio_fallback
		mh_audio_assets
		mh_audio_ffmpeg
		mh_resource_filesystem
		mh_resource_zip
		mh_resource_memory)
	mh_c89(mh-opus-audio-check)

	add_executable(mh-webm-video-check tests/integration/webm_video.c)
	target_link_libraries(mh-webm-video-check PRIVATE
		mh_video_ffmpeg
		mh_video_divi
		mh_resource_filesystem
		mh_resource_zip)
	mh_c89(mh-webm-video-check)

	add_executable(mh-opus-encode-check tests/integration/opus_encode.c)
	target_link_libraries(mh-opus-encode-check PRIVATE
		mh_audio_ffmpeg_encoding
		mh_audio_ffmpeg
		mh_audio_wave
		mh_resource_filesystem)
	mh_c89(mh-opus-encode-check)

	add_executable(mh-webm-encode-check tests/integration/webm_encode.c)
	target_link_libraries(mh-webm-encode-check PRIVATE
		mh_video_ffmpeg_encoding
		mh_video_ffmpeg
		mh_resource_filesystem)
	mh_c89(mh-webm-encode-check)
	find_program(MH_TEST_FFMPEG NAMES ffmpeg)

	if(Python3_Interpreter_FOUND AND MH_TEST_FFMPEG AND
		NOT CMAKE_CROSSCOMPILING)
		add_test(NAME compact-opus-audio COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/assets/check_compact_audio.py"
			--decoder $<TARGET_FILE:mh-opus-audio-check> --ffmpeg
				"${MH_TEST_FFMPEG}")
		set_tests_properties(compact-opus-audio PROPERTIES TIMEOUT 120)
		add_test(NAME compact-webm-video COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/assets/check_compact_video.py"
			--decoder $<TARGET_FILE:mh-webm-video-check> --ffmpeg
				"${MH_TEST_FFMPEG}")
		set_tests_properties(compact-webm-video PROPERTIES TIMEOUT 120)
		add_test(NAME compact-opus-encoding COMMAND
			"${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/assets/check_opus_encoding.py"
			--encoder $<TARGET_FILE:mh-opus-encode-check> --ffmpeg
				"${MH_TEST_FFMPEG}")
		set_tests_properties(compact-opus-encoding PROPERTIES TIMEOUT
			120 SKIP_RETURN_CODE 77)
		add_test(NAME compact-webm-encoding COMMAND
			"${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/assets/check_webm_encoding.py"
			--encoder $<TARGET_FILE:mh-webm-encode-check>
			--decoder $<TARGET_FILE:mh-webm-video-check> --ffmpeg
				"${MH_TEST_FFMPEG}")
		set_tests_properties(compact-webm-encoding PROPERTIES TIMEOUT
			120 SKIP_RETURN_CODE 77)
	endif()
endif()
