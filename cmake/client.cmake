# Service choices shared by the SDL host and diagnostic entry points.
if(TARGET mh_clock_system)
	add_library(mh_host_clock STATIC src/client/hosts/clock.c)
	target_link_libraries(mh_host_clock PUBLIC mh_clock_system)
	mh_c89(mh_host_clock)
endif()

add_library(mh_host_user_data STATIC src/client/hosts/storage.c)
target_link_libraries(mh_host_user_data PUBLIC mh_user_data)
mh_c89(mh_host_user_data)

if(TARGET mh_user_data_filesystem)
	target_compile_definitions(mh_host_user_data PRIVATE
		MH_HAVE_USER_DATA_FILES=1)
	target_link_libraries(mh_host_user_data PRIVATE mh_user_data_filesystem)
endif()

add_library(mh_host_capabilities STATIC src/client/hosts/capabilities.c)
target_include_directories(mh_host_capabilities PUBLIC src)
mh_c89(mh_host_capabilities)

add_library(mh_host_network STATIC src/client/hosts/network.c)
target_link_libraries(mh_host_network PRIVATE mh_host_capabilities)
mh_c89(mh_host_network)

if(MH_BUILD_NETWORK AND TARGET mh_transport_udp AND
	(TARGET mh_resolver_posix OR TARGET mh_resolver_windows) AND
	TARGET mh_clock_system)
	target_compile_definitions(mh_host_capabilities PRIVATE
		MH_HOST_NETWORK=1)
	set(client_network_available ON)
	target_compile_definitions(mh_host_network PRIVATE MH_HOST_LANINET=1)
	target_link_libraries(mh_host_network PRIVATE
		mh_transport_udp
		mh_clock_system)

	if(TARGET mh_resolver_windows)
		target_link_libraries(mh_host_network PRIVATE
			mh_resolver_windows)
	else()
		target_link_libraries(mh_host_network PRIVATE mh_resolver_posix)
	endif()
endif()

add_library(mh_host_screenshots STATIC src/client/hosts/screenshots.c)
target_include_directories(mh_host_screenshots PUBLIC src)
target_link_libraries(mh_host_screenshots PRIVATE mh_screenshot_build)

foreach(format IN LISTS MH_SCREENSHOT_FORMATS)
	target_link_libraries(mh_host_screenshots PRIVATE mh_image_${format})
endforeach()
mh_c89(mh_host_screenshots)

add_library(mh_progress STATIC src/client/session/progress.c)
target_link_libraries(mh_progress PUBLIC
	mh_catalog_loading
	mh_ui
	mh_track_flag_storage)
mh_c89(mh_progress)

add_library(mh_race_records STATIC src/client/session/records.c)
target_link_libraries(mh_race_records PUBLIC
	mh_sim
	mh_record_storage
	mh_highscore_storage)
mh_c89(mh_race_records)

# Portable session coordination is also used by viewer-disabled checks.
add_library(mh_demo_recording STATIC src/client/session/demo_recording.c)
target_link_libraries(mh_demo_recording PUBLIC mh_demo_sim mh_demo_storage)
mh_c89(mh_demo_recording)

add_library(mh_playback_hud STATIC src/client/session/playback_hud.c)
target_link_libraries(mh_playback_hud PUBLIC mh_ui mh_demo_recording)
mh_c89(mh_playback_hud)

add_library(mh_leagues STATIC src/client/session/leagues.c)
target_link_libraries(mh_leagues PUBLIC mh_league_storage)
mh_c89(mh_leagues)

add_library(mh_client_diagnostics STATIC src/client/diagnostics/options.c)
target_include_directories(mh_client_diagnostics PUBLIC src)
target_link_libraries(mh_client_diagnostics PUBLIC mh_error mh_arguments)
mh_c89(mh_client_diagnostics)

add_library(mh_settings_store STATIC src/client/settings/store.c)
target_link_libraries(mh_settings_store PUBLIC
	mh_settings_storage
	mh_asset_loaders)
mh_c89(mh_settings_store)

add_library(mh_control_profiles STATIC src/client/settings/controls.c)
target_link_libraries(mh_control_profiles PUBLIC
	mh_control_store
	mh_user_data
	mh_input)
mh_c89(mh_control_profiles)

add_library(mh_activity STATIC
	src/client/application/activity.c
	src/client/application/activity_run.c)
target_include_directories(mh_activity PUBLIC src)
target_link_libraries(mh_activity PUBLIC mh_error PRIVATE mh_clock_source)
mh_c89(mh_activity)

# Startup is host-driven and can run with SDK-free service providers.
add_library(mh_opening STATIC src/client/application/opening.c)
target_link_libraries(mh_opening
	PUBLIC
		mh_activity
	PRIVATE
		mh_video_dispatch
		mh_resource_source
		mh_window_system
		mh_ui)
mh_c89(mh_opening)

# Portable race presentation and session context; no native service selection.
add_library(mh_application STATIC
	src/client/loading/view.c
	src/client/loading/confirmation.c
	src/client/race/startup.c
	src/client/session/loading.c
	src/client/settings/availability.c
	src/client/settings/render_choice.c
	src/client/session/race_context.c
	src/client/screenshots/screenshot.c
	src/client/race/race.c
	src/client/race/camera.c
	src/client/race/controls.c
	src/client/race/events.c
	src/client/race/frame.c
	src/client/race/frame_rate.c
	src/client/race/pacing.c
	src/client/race/feedback.c
	src/client/race/finish.c
	src/client/race/hud.c
	src/client/race/pause.c
	src/client/race/render.c
	src/client/race/results.c
	src/client/race/simulation.c
	src/client/race/setup.c
	src/client/race/timing.c
	src/client/race/trace.c
	src/client/race/console.c
	src/client/session/season_screen.c)
if("software" IN_LIST MH_RENDER_PROFILES OR BUILD_TESTING OR MH_BUILD_TOOLS)
	target_sources(mh_application PRIVATE
		src/client/race/software_hud.c
		src/client/race/software_scene.c)
endif()
target_link_libraries(mh_application PUBLIC
	mh_image_encoding
	mh_ui
	mh_audio_playback
	mh_input
	mh_settings
	mh_settings_store
	mh_picture_storage
	mh_asset_loaders
	mh_race_scene
	mh_ui_assets
	mh_recording_paths
	mh_game_timer
	mh_activity
	mh_control_profiles
	mh_race_records
	mh_progress
	mh_renderer
	mh_window_system
	mh_graphics_catalog)
mh_c89(mh_application)

if(client_network_available OR (TARGET mh_transport_browser AND
	"openmh" IN_LIST MH_NETWORK_PROTOCOLS))
	set_property(SOURCE src/client/application/arguments.c APPEND PROPERTY
		COMPILE_DEFINITIONS MH_CLIENT_NETWORK=1)
endif()
target_link_libraries(mh_application PRIVATE mh_render_build)

add_library(mh_session_race_options STATIC src/client/session/race_options.c)
target_link_libraries(mh_session_race_options PUBLIC mh_error)
mh_c89(mh_session_race_options)

add_library(mh_single_setup STATIC src/client/session/single_setup.c)
target_link_libraries(mh_single_setup PUBLIC mh_application)
mh_c89(mh_single_setup)

add_library(mh_single_session STATIC src/client/session/single_session.c)
target_sources(mh_single_session PRIVATE src/client/session/attract_session.c)
target_link_libraries(mh_single_session PUBLIC
	mh_session_race_options
	mh_single_setup
	mh_playback_hud
	mh_activity)
target_link_libraries(mh_single_session PRIVATE mh_client_diagnostics)
mh_c89(mh_single_session)

add_library(mh_league_setup STATIC src/client/session/league_setup.c)
target_link_libraries(mh_league_setup PUBLIC mh_application)
mh_c89(mh_league_setup)

add_library(mh_league_session STATIC src/client/session/league_session.c)
target_link_libraries(mh_league_session PUBLIC
	mh_session_race_options
	mh_video_dispatch
	mh_league_setup
	mh_leagues
	mh_playback_hud
	mh_progress
	mh_activity)
target_link_libraries(mh_league_session PRIVATE mh_client_diagnostics)
mh_c89(mh_league_session)

add_library(mh_demo_setup STATIC src/client/session/demo_setup.c)
target_link_libraries(mh_demo_setup PUBLIC mh_demo_sim mh_application)
mh_c89(mh_demo_setup)

add_library(mh_demo_benchmark_report STATIC src/client/session/demo_benchmark.c)
target_link_libraries(mh_demo_benchmark_report PUBLIC
	mh_demo_sim
	mh_application)
mh_c89(mh_demo_benchmark_report)

add_library(mh_demo_session STATIC src/client/session/demo_session.c)
target_link_libraries(mh_demo_session PUBLIC
	mh_session_race_options
	mh_demo_setup
	mh_single_setup
	mh_playback_hud
	mh_activity
	mh_demo_benchmark_report)
target_link_libraries(mh_demo_session PRIVATE mh_client_diagnostics)
mh_c89(mh_demo_session)

add_library(mh_network_setup STATIC src/client/session/network_setup.c)
target_link_libraries(mh_network_setup PUBLIC mh_net_sim mh_application)
mh_c89(mh_network_setup)

add_library(mh_network_race STATIC src/client/session/network_race.c)
target_link_libraries(mh_network_race PUBLIC
	mh_network_setup
	mh_network_session
	mh_demo_recording)
mh_c89(mh_network_race)

add_library(mh_network_lobby STATIC src/client/session/lobby.c)
target_link_libraries(mh_network_lobby
	PUBLIC
		mh_image_capture
		mh_canvas
		mh_network_session
		mh_lobby_ui
		mh_application
		mh_activity
	PRIVATE
		mh_font_loading)
mh_c89(mh_network_lobby)

add_library(mh_network_game STATIC src/client/session/network_game.c)
target_link_libraries(mh_network_game PUBLIC mh_network_race mh_network_lobby)
target_link_libraries(mh_network_game PRIVATE mh_client_diagnostics)
mh_c89(mh_network_game)

add_library(mh_menus_app STATIC
	src/client/application/application.c
	src/client/application/race_frontend.c
	src/client/application/multiplayer.c
	src/client/application/race_frontend_catalog.c
	src/client/application/race_frontend_details.c
	src/client/application/race_frontend_settings.c
	src/client/application/preferences.c
	src/client/settings/preferences.c
	src/client/application/host.c
	src/client/application/arguments.c
	src/client/application/content.c
	src/client/application/menu_setup.c
	src/client/application/settings.c
	src/client/application/window.c
	src/client/application/menu_frame.c
	src/client/menus/compact.c
	src/client/menus/configuration.c
	src/client/menus/credits.c
	src/client/menus/rankings.c
	src/client/menus/diagnostics.c
	src/client/menus/pages.c
	src/client/menus/input.c
	src/client/menus/launch.c
	src/client/menus/league.c
	src/client/menus/main.c
	src/client/menus/dialogs.c
	src/client/menus/multiplayer.c
	src/client/menus/controls.c
	src/client/menus/options.c
	src/client/menus/personal.c
	src/client/menus/graphics.c
	src/client/menus/sound.c
	src/client/menus/settings.c
	src/client/menus/race_setup.c
	src/client/menus/recordings.c
	src/client/menus/ghost.c
	src/client/menus/resources.c
	src/client/menus/catalogs.c
	src/client/menus/unlocks.c)
target_include_directories(mh_menus_app PUBLIC src)

if(MH_DEBUG)
	target_compile_definitions(mh_menus_app PRIVATE MH_DEBUG=1)
endif()
target_link_libraries(mh_menus_app PRIVATE
	mh_arguments
	mh_client_diagnostics
	mh_icon
	mh_palette_loading
	mh_menu_layout_loading
	mh_vfs
	mh_content
	mh_opening
	mh_image_capture
	mh_canvas
	mh_application
	mh_recording_catalog
	mh_storage
	mh_address_store
	mh_control_profiles
	mh_highscore_storage)
target_link_libraries(mh_menus_app PRIVATE mh_progress)
mh_c89(mh_menus_app)
target_link_libraries(mh_menus_app PRIVATE
	mh_build_version mh_single_session mh_demo_session)
target_link_libraries(mh_menus_app PRIVATE mh_league_session)
target_link_libraries(mh_menus_app PRIVATE mh_leagues)
target_link_libraries(mh_menus_app PRIVATE mh_network_game)

if(MH_BUILD_CLIENT)
	include(cmake/sdl.cmake)

	if(EMSCRIPTEN)
		include(cmake/browser_runtime.cmake)
	else()
		add_executable(openmotor src/client/hosts/sdl_main.c)

		if(WIN32)
			set_target_properties(openmotor PROPERTIES
				WIN32_EXECUTABLE TRUE)
		endif()
		target_link_libraries(openmotor PRIVATE
			mh_host_sdl
			mh_window_sdl_startup)
		if(TARGET mh_shaders)
			add_dependencies(openmotor mh_shaders)
		endif()
		mh_c89(openmotor)
	endif()
endif()

# Select optional playback for the game independently of diagnostic runners.
if(MH_BUILD_CLIENT)
	add_library(mh_host_sdl_video STATIC src/client/hosts/sdl_video.c)
	target_link_libraries(mh_host_sdl_video
		PUBLIC
			mh_video_dispatch
		PRIVATE
			mh_host_capabilities)
	mh_c89(mh_host_sdl_video)

	if(MH_HAVE_FFMPEG AND MH_DECODE_MOVIES)
		target_compile_definitions(mh_host_sdl_video PRIVATE
			MH_HOST_VIDEO=1)
		target_compile_definitions(mh_host_capabilities PRIVATE
			MH_HOST_VIDEO=1)
		target_link_libraries(mh_host_sdl_video PRIVATE
			mh_video_playback
			mh_video_divi
			mh_video_ffmpeg
			mh_canvas_sdl
			mh_window_sdl
			mh_image_capture
			mh_input_sdl
			mh_clock_system
			mh_audio_sdl)
	endif()
endif()
