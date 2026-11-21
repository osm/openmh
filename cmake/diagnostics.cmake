# Diagnostic adapters for tools and tests; the game host does not link these.
# Native defaults are separate from the SDK-free dispatch/session owner.
if(MH_BUILD_NETWORK AND TARGET mh_transport_udp AND
	(TARGET mh_resolver_posix OR TARGET mh_resolver_windows) AND
	TARGET mh_clock_system)

	add_library(mh_diagnostic_network STATIC
		src/client/diagnostics/runtime_network.c)
	target_link_libraries(mh_diagnostic_network
		PUBLIC
			mh_network_session
		PRIVATE
			mh_host_network)
	mh_c89(mh_diagnostic_network)
endif()

if(TARGET mh_data_directory_filesystem)
	add_library(mh_picture_files STATIC
		src/client/diagnostics/storage_picture_store.c)
	target_link_libraries(mh_picture_files
		PUBLIC
			mh_picture_storage
		PRIVATE
			mh_data_directory_filesystem)
	mh_c89(mh_picture_files)

	add_library(mh_control_files STATIC
		src/client/diagnostics/storage_control_store.c)
	target_link_libraries(mh_control_files
		PUBLIC
			mh_control_store
		PRIVATE
			mh_resource_filesystem
			mh_data_directory_filesystem)
	mh_c89(mh_control_files)

	add_library(mh_address_files STATIC
		src/client/diagnostics/storage_address_store.c)
	target_link_libraries(mh_address_files
		PUBLIC
			mh_address_store
		PRIVATE
			mh_data_directory_filesystem)
	mh_c89(mh_address_files)
endif()

# Compatibility storage entry points select the native writable provider here.
if(TARGET mh_user_data_filesystem)
	add_library(mh_diagnostic_leagues STATIC
		src/client/diagnostics/storage_leagues.c)
	target_link_libraries(mh_diagnostic_leagues
		PUBLIC
			mh_leagues
		PRIVATE
			mh_user_data_filesystem)
	mh_c89(mh_diagnostic_leagues)

	add_library(mh_league_files STATIC
		src/client/diagnostics/storage_league_file.c
		src/client/diagnostics/storage_league_store.c
		src/client/diagnostics/storage_league_date.c)
	target_link_libraries(mh_league_files
		PUBLIC
			mh_league_storage
		PRIVATE
			mh_user_data_filesystem
			mh_clock_system)
	mh_c89(mh_league_files)

	add_library(mh_track_flag_files STATIC
		src/client/diagnostics/storage_track_flag_file.c)
	target_link_libraries(mh_track_flag_files
		PUBLIC
			mh_track_flag_storage
		PRIVATE
			mh_user_data_filesystem)
	mh_c89(mh_track_flag_files)

	add_library(mh_record_files STATIC
		src/client/diagnostics/storage_record_file.c)
	target_link_libraries(mh_record_files
		PUBLIC
			mh_record_storage
		PRIVATE
			mh_user_data_filesystem)
	mh_c89(mh_record_files)

	add_library(mh_highscore_files STATIC
		src/client/diagnostics/storage_highscore_file.c)
	target_link_libraries(mh_highscore_files
		PUBLIC
			mh_highscore_storage
		PRIVATE
			mh_user_data_filesystem)
	mh_c89(mh_highscore_files)

	add_library(mh_settings_files STATIC
		src/client/diagnostics/storage_settings_file.c)
	target_link_libraries(mh_settings_files
		PUBLIC
			mh_settings_storage
		PRIVATE
			mh_user_data_filesystem)
	mh_c89(mh_settings_files)
endif()

add_library(mh_diagnostic_assets STATIC
	src/client/diagnostics/asset_path.c
	src/client/diagnostics/asset_config_load.c)
target_sources(mh_diagnostic_assets PRIVATE
	src/client/diagnostics/asset_scene.c
	src/client/diagnostics/asset_ui.c
	src/client/diagnostics/asset_audio.c
	src/client/diagnostics/asset_mesh_load.c
	src/client/diagnostics/asset_horn_catalog.c
	src/client/diagnostics/asset_profile_catalog.c)
target_link_libraries(mh_diagnostic_assets
	PRIVATE
		mh_retro_fallback
		mh_catalog_fallback
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
	PRIVATE
		mh_collision_fallback
		mh_model_myo
		mh_world_myw
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_wireframe_lob
	PUBLIC
		mh_resource_filesystem
		mh_catalog_loading
		mh_asset_loaders
		mh_audio_assets
		mh_race_scene
		mh_ui_assets
	PRIVATE
		mh_audio_fallback
		mh_image_fallback
		mh_image_pdi
		mh_font_fallback
		mh_sprite_spr)
mh_c89(mh_diagnostic_assets)

if(TARGET mh_data_directory_filesystem)
	add_library(mh_demo_files STATIC
		src/client/diagnostics/storage_demo_file.c)
	target_link_libraries(mh_demo_files PRIVATE
		mh_data_directory_filesystem)
else()
	add_library(mh_demo_files STATIC
		src/client/diagnostics/storage_demo_file_stub.c)
endif()
target_link_libraries(mh_demo_files
	PUBLIC
		mh_demo_storage
	PRIVATE
		mh_recording_paths)
mh_c89(mh_demo_files)

add_library(mh_diagnostic_demo_recording STATIC
	src/client/diagnostics/storage_demo_recording.c)
target_link_libraries(mh_diagnostic_demo_recording
	PUBLIC
		mh_demo_recording
	PRIVATE
		mh_demo_files)
mh_c89(mh_diagnostic_demo_recording)

if(TARGET mh_data_directory_filesystem)
	add_library(mh_recording_catalog_files STATIC
		src/client/diagnostics/storage_recording_catalog.c)
	target_link_libraries(mh_recording_catalog_files PRIVATE
		mh_data_directory_filesystem)
else()
	add_library(mh_recording_catalog_files STATIC
		src/client/diagnostics/storage_recording_catalog_stub.c)
endif()
target_link_libraries(mh_recording_catalog_files PUBLIC mh_recording_catalog)
mh_c89(mh_recording_catalog_files)

if(MH_BUILD_CLIENT)
	# Input and window services share the selected SDL lifetime.
	add_library(mh_diagnostic_input STATIC
		src/client/diagnostics/input.c)
	mh_c89(mh_diagnostic_input)
	target_link_libraries(mh_diagnostic_input
		PUBLIC
			mh_input
			mh_window_system
		PRIVATE
			mh_input_sdl
			mh_window_sdl
			mh_host_clock
			mh_host_sdl_clipboard)

	add_library(mh_diagnostic_graphics_catalog STATIC
		src/client/diagnostics/render_graphics_catalog.c)
	mh_c89(mh_diagnostic_graphics_catalog)
	target_link_libraries(mh_diagnostic_graphics_catalog
		PUBLIC
			mh_graphics_catalog
		PRIVATE
			mh_window_sdl)

	add_library(mh_diagnostic_runtime STATIC
		src/client/diagnostics/render_capture.c
		src/client/diagnostics/runtime_race_context.c
		src/client/diagnostics/model_viewer.c
		src/client/diagnostics/runtime_single_setup.c
		src/client/diagnostics/runtime_league_setup.c
		src/client/diagnostics/runtime_demo_setup.c
		src/client/diagnostics/runtime_network_setup.c
		src/client/diagnostics/runtime_lobby.c
		src/client/diagnostics/storage_controls.c
		src/client/diagnostics/storage_recording_path.c
		src/client/diagnostics/runtime_records.c
		src/client/diagnostics/runtime_progress.c
		src/client/diagnostics/render_renderer.c
		src/client/diagnostics/media_audio.c
		src/client/diagnostics/runtime_loading.c
		src/client/diagnostics/runtime_race.c
		src/client/diagnostics/runtime_season_screen.c
		src/client/diagnostics/runtime_game_clock.c
		src/client/diagnostics/storage_settings_store.c)
	mh_c89(mh_diagnostic_runtime)
	target_link_libraries(mh_diagnostic_runtime
		PRIVATE
			mh_retro_fallback
			mh_menu_layout_dta
			mh_palette_act
			mh_catalog_fallback
			mh_horn_fallback
			mh_material_mat_srf
			mh_environment_esd
			mh_collision_fallback
			mh_model_myo
			mh_world_myw
		PRIVATE
			mh_image_tga
			mh_host_screenshots
			mh_image_fallback
			mh_image_pdi
			mh_audio_fallback
			mh_application
			mh_single_setup
			mh_league_setup
			mh_demo_setup
			mh_network_setup
			mh_network_lobby
			mh_diagnostic_assets
			mh_client_diagnostics
			mh_host_user_data
			mh_host_capabilities
			mh_render_sdl
			mh_render_software
			mh_asset_loaders
			mh_race_scene
			mh_ui_assets
			mh_audio_assets
			mh_recording_paths
			mh_diagnostic_demo_recording
			mh_game_timer
			mh_audio_sdl
			mh_activity
			mh_control_profiles
			mh_race_records
			mh_progress)
	target_link_libraries(mh_diagnostic_runtime PUBLIC
		mh_ui
		mh_audio
		mh_input
		mh_settings
		mh_diagnostic_input
		mh_diagnostic_graphics_catalog)

	if(TARGET mh_record_files AND TARGET mh_highscore_files)
		target_link_libraries(mh_diagnostic_runtime PRIVATE
			mh_record_files
			mh_highscore_files)
	endif()

	if(MH_DEBUG AND NOT EMSCRIPTEN)
		# Session entry points belong to the debug client, never to separate programs.
		add_library(mh_debug_sessions STATIC
			src/client/diagnostics/session.c
			src/client/diagnostics/session_race.c
			src/client/diagnostics/session_race_options.c
			src/client/diagnostics/session_race_steps.c
			src/client/diagnostics/session_demo.c)
		target_link_libraries(mh_debug_sessions PRIVATE
			mh_catalog_fallback
			mh_horn_fallback
			mh_material_mat_srf
			mh_environment_esd
			mh_collision_fallback
			mh_model_myo
			mh_world_myw
			mh_atlas_remap
			mh_navigation_mot_dat
			mh_animation_lightwave
			mh_diagnostic_runtime
			mh_playback_hud
			mh_demo_benchmark_report
			mh_user_data_filesystem
			mh_file_read_filesystem
			mh_demo_setup)

		if(UNIX)
			target_sources(mh_debug_sessions PRIVATE
				src/client/diagnostics/session_league.c)
			target_compile_definitions(mh_debug_sessions PRIVATE
				MH_DEBUG_LEAGUE=1)
			target_link_libraries(mh_debug_sessions PRIVATE
				mh_league_setup
				mh_league_files
				mh_progress)
		endif()

		if(TARGET mh_diagnostic_network)
			target_sources(mh_debug_sessions PRIVATE
				src/client/diagnostics/session_network.c)
			target_compile_definitions(mh_debug_sessions PRIVATE
				MH_DEBUG_NETWORK=1)
			target_link_libraries(mh_debug_sessions PRIVATE
				mh_network_race
				mh_network_lobby
				mh_diagnostic_network)
		endif()
		mh_c89(mh_debug_sessions)
		target_compile_definitions(openmotor PRIVATE MH_DEBUG=1)
		target_link_libraries(openmotor PRIVATE
			mh_debug_sessions
			mh_filesystem)

		if(BUILD_TESTING AND Python3_Interpreter_FOUND)
			set(session_test_options)

			if(CMAKE_CROSSCOMPILING)
				get_target_property(session_test_emulator
					openmotor CROSSCOMPILING_EMULATOR)

				if(session_test_emulator)
					foreach(argument IN LISTS
						session_test_emulator)
						list(APPEND
							session_test_options
							"--runner=${argument}")
					endforeach()
				endif()
			endif()

			if(UNIX)
				list(APPEND session_test_options --league)
			endif()

			if(TARGET mh_diagnostic_network)
				list(APPEND session_test_options --network)
			endif()
			add_test(NAME client-debug-sessions
				COMMAND "${Python3_EXECUTABLE}"
					"${PROJECT_SOURCE_DIR}/tests/integration/client_sessions.py"
					"$<TARGET_FILE:openmotor>"
						${session_test_options})
		endif()
	endif()
endif()

if(TARGET mh_diagnostic_runtime)
	target_link_libraries(mh_diagnostic_runtime PRIVATE mh_host_sdl_video)
endif()

if(MH_HAVE_FFMPEG AND TARGET mh_host_sdl_video)
	add_library(mh_diagnostic_video STATIC
		src/client/diagnostics/media_video.c)
	target_link_libraries(mh_diagnostic_video
		PUBLIC
			mh_video_dispatch
		PRIVATE
			mh_host_sdl_video
			mh_resource_filesystem
			mh_host_clock)
	mh_c89(mh_diagnostic_video)

	if(TARGET mh_debug_sessions)
		target_link_libraries(mh_debug_sessions PRIVATE
			mh_diagnostic_video)
		target_compile_definitions(mh_debug_sessions PRIVATE
			MH_HAVE_VIDEO=1)
	endif()
endif()
