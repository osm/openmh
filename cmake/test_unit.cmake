# A fixture may require a dialect or a combination (for interoperability).
function(mh_protocol_test protocols name)
	foreach(protocol IN LISTS protocols)
		if(NOT protocol IN_LIST MH_NETWORK_PROTOCOLS)
			return()
		endif()
	endforeach()
	add_test(NAME ${name} COMMAND ${ARGN})
endfunction()

if(BUILD_TESTING)
	add_executable(test-network-profiles tests/network/protocol_profiles.c)
	target_link_libraries(test-network-profiles PRIVATE
		mh_network_build mh_net_protocol mh_server_config)
	mh_c89(test-network-profiles)
	add_test(NAME network-protocol-selection COMMAND test-network-profiles)

	add_executable(test-udp-multiplex
		tests/network/udp_multiplex.c
		src/backends/network/udp_multiplex.c)
	target_link_libraries(test-udp-multiplex PRIVATE mh_error)
	mh_c89(test-udp-multiplex)
	add_test(NAME udp-multiplex COMMAND test-udp-multiplex)

	add_executable(test-network-channel tests/network/channel.c)
	target_link_libraries(test-network-channel PRIVATE
		mh_transport_dispatch)
	mh_c89(test-network-channel)
	add_test(NAME network-channel COMMAND test-network-channel)

	foreach(mode enabled disabled)
		add_executable(test-browser-multiplayer-${mode}
			tests/browser/multiplayer_api.c
			src/client/hosts/browser_multiplayer_api.c)
		target_link_libraries(test-browser-multiplayer-${mode} PRIVATE
			mh_text)

		if(mode STREQUAL "enabled")
			target_compile_definitions(test-browser-multiplayer-${mode} PRIVATE
				MH_BROWSER_NETWORK=1)
		endif()
		mh_c89(test-browser-multiplayer-${mode})
		add_test(NAME browser-multiplayer-${mode} COMMAND
			test-browser-multiplayer-${mode})
	endforeach()
	find_program(MH_TEST_NODE NAMES node nodejs)

	if(MH_TEST_NODE)
		add_test(NAME browser-multiplayer-player COMMAND "${MH_TEST_NODE}"
			"${CMAKE_SOURCE_DIR}/tests/browser/multiplayer.js"
				"${CMAKE_SOURCE_DIR}/src/client/hosts")
	endif()

	add_executable(test-arguments tests/core/arguments.c)
	target_link_libraries(test-arguments PRIVATE mh_arguments)
	mh_c89(test-arguments)
	add_test(NAME command-line-parser COMMAND test-arguments)

	add_executable(test-server-arguments
		tests/server/arguments.c
		src/server/cli/arguments.c)
	target_link_libraries(test-server-arguments PRIVATE
		mh_server_config
		mh_build_version
		mh_arguments
		mh_server_console_output)
	mh_c89(test-server-arguments)
	add_test(NAME server-arguments COMMAND test-server-arguments)

	if(TARGET openmotorsv AND NOT CMAKE_CROSSCOMPILING)
		add_test(NAME server-startup-defaults
			COMMAND "${Python3_EXECUTABLE}" -m tests.server.startup
				"$<TARGET_FILE:openmotorsv>")
		set_tests_properties(server-startup-defaults PROPERTIES
			WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 60)
	endif()

	add_executable(test-server-console-output tests/server/console_output.c)
	target_link_libraries(test-server-console-output PRIVATE
		mh_server_console_output)
	mh_c89(test-server-console-output)
	add_test(NAME server-console-output COMMAND test-server-console-output)

	add_executable(mh-simulation-timing-check
		tests/integration/simulation_timing.c)
	target_link_libraries(mh-simulation-timing-check PRIVATE
		mh_diagnostic_assets
		mh_sim
		mh_input)
	mh_c89(mh-simulation-timing-check)

	if(EMSCRIPTEN)
		# Headless qualification borrows the same read-only assets as native.
		# Never enable host filesystem access in the browser application.
		target_link_options(mh-simulation-timing-check PRIVATE
			-sNODERAWFS=1)
	endif()

	add_executable(test-png-encoder tests/unit/test_png_encoder.c)
	target_link_libraries(test-png-encoder PRIVATE mh_image_png ZLIB::ZLIB)
	mh_c89(test-png-encoder)
	add_test(NAME png-encoder COMMAND test-png-encoder)

	# Compile both parser variants regardless of the selected client build.
	foreach(mode player debug)
		add_executable(test-client-arguments-${mode}
			tests/integration/client_arguments.c
			src/client/application/arguments.c)
		target_link_libraries(test-client-arguments-${mode} PRIVATE
			mh_build_version
			mh_application
			mh_transport_dispatch
			mh_net_protocol
			mh_client_diagnostics)

		if(mode STREQUAL "debug")
			target_compile_definitions(test-client-arguments-${mode} PRIVATE
				MH_DEBUG=1)
		endif()
		mh_c89(test-client-arguments-${mode})
		add_test(NAME client-arguments-${mode} COMMAND
			test-client-arguments-${mode})
		string(CONCAT client_test_environment
			"MH_DEBUG_WINDOW_TITLES=1;"
			"MH_DEBUG_CAMERA_TRACE=1;"
			"MH_DEBUG_MENU_CLOCK=1;"
			"MH_DEBUG_TEXTURE_BYTES=0;"
			"MH_SCREENSHOT_DIR=ignored")
		set_tests_properties(client-arguments-${mode} PROPERTIES
			ENVIRONMENT
			"${client_test_environment}")
	endforeach()

	add_executable(test-tick-clock tests/unit/test_tick_clock.c)
	target_link_libraries(test-tick-clock PRIVATE mh_sim)
	mh_c89(test-tick-clock)
	add_test(NAME simulation-tick-clock COMMAND test-tick-clock)

	add_executable(test-tick-input tests/unit/test_tick_input.c)
	target_link_libraries(test-tick-input PRIVATE mh_input)
	mh_c89(test-tick-input)
	add_test(NAME simulation-tick-input COMMAND test-tick-input)

	add_executable(test-fixed-frame tests/unit/test_fixed_frame.c)
	target_link_libraries(test-fixed-frame PRIVATE mh_sim)
	mh_c89(test-fixed-frame)
	add_test(NAME simulation-fixed-frame COMMAND test-fixed-frame)

	add_executable(test-scene-interpolation
		tests/unit/test_scene_interpolation.c)
	target_link_libraries(test-scene-interpolation PRIVATE mh_race_scene)
	mh_c89(test-scene-interpolation)
	add_test(NAME simulation-scene-interpolation COMMAND
		test-scene-interpolation)

	add_executable(test-race-pacing
		tests/unit/test_race_pacing.c
		src/client/race/pacing.c)
	target_link_libraries(test-race-pacing PRIVATE mh_clock_source)
	mh_c89(test-race-pacing)
	add_test(NAME race-render-pacing COMMAND test-race-pacing)

	add_executable(test-frame-rate
		tests/unit/test_frame_rate.c
		src/client/race/frame_rate.c)
	target_link_libraries(test-frame-rate PRIVATE mh_ui)
	mh_c89(test-frame-rate)
	add_test(NAME race-frame-rate COMMAND test-frame-rate)

	add_executable(test-server-config tests/server/config.c)
	target_link_libraries(test-server-config PRIVATE mh_server_config)
	mh_c89(test-server-config)
	add_test(NAME server-configuration COMMAND test-server-config)

	add_executable(test-server-bans tests/server/bans.c)
	target_link_libraries(test-server-bans PRIVATE mh_server_session)
	mh_c89(test-server-bans)
	add_test(NAME server-bans COMMAND test-server-bans)

	add_executable(test-host-common tests/unit/test_host_common.c)
	target_link_libraries(test-host-common PRIVATE mh_net mh_server_session)
	mh_c89(test-host-common)
	mh_protocol_test("legacy" network-host-common test-host-common)

	add_executable(test-server-admission tests/server/admission.c)
	target_link_libraries(test-server-admission PRIVATE mh_server_session)
	mh_c89(test-server-admission)
	mh_protocol_test("legacy" server-admission test-server-admission)

	add_executable(test-server-lobby tests/server/lobby.c)
	target_link_libraries(test-server-lobby PRIVATE mh_server_session)
	mh_c89(test-server-lobby)
	mh_protocol_test("legacy;openmh" server-lobby test-server-lobby)

	add_executable(test-server-heartbeat tests/server/heartbeat.c)
	target_link_libraries(test-server-heartbeat PRIVATE mh_server_session)
	mh_c89(test-server-heartbeat)
	add_test(NAME server-heartbeat COMMAND test-server-heartbeat)

	add_executable(test-server-clock tests/server/clock.c)
	target_link_libraries(test-server-clock PRIVATE mh_server_session)
	mh_c89(test-server-clock)
	add_test(NAME server-race-clock COMMAND test-server-clock)

	add_executable(test-server-vehicle tests/server/vehicle.c)
	target_link_libraries(test-server-vehicle PRIVATE mh_server_session)
	mh_c89(test-server-vehicle)
	add_test(NAME server-race-vehicle COMMAND test-server-vehicle)

	add_executable(test-server-progress tests/server/progress.c)
	target_link_libraries(test-server-progress PRIVATE mh_server_session)
	mh_c89(test-server-progress)
	add_test(NAME server-race-progress COMMAND test-server-progress)

	add_executable(test-server-standings tests/server/standings.c)
	target_link_libraries(test-server-standings PRIVATE mh_server_session)
	mh_c89(test-server-standings)
	add_test(NAME server-race-standings COMMAND test-server-standings)

	add_executable(test-server-race-log tests/server/race_log.c)
	target_link_libraries(test-server-race-log PRIVATE mh_server_session)
	mh_c89(test-server-race-log)
	add_test(NAME server-race-log COMMAND test-server-race-log)

	add_executable(test-server-results tests/server/results.c)
	target_link_libraries(test-server-results PRIVATE
		mh_server_session
		mh_net)
	mh_c89(test-server-results)
	add_test(NAME server-race-results COMMAND test-server-results)

	add_executable(test-server-lifecycle tests/server/lifecycle.c)
	target_link_libraries(test-server-lifecycle PRIVATE
		mh_server_session
		mh_net)
	mh_c89(test-server-lifecycle)
	mh_protocol_test("legacy" server-race-lifecycle test-server-lifecycle)
	mh_protocol_test("openmh" server-race-lifecycle-openmh
		test-server-lifecycle --openmh)

	add_executable(test-server-return tests/server/return.c)
	target_link_libraries(test-server-return PRIVATE
		mh_server_session
		mh_net)
	mh_c89(test-server-return)
	mh_protocol_test("legacy" server-lobby-return test-server-return)

	add_executable(test-server-ready tests/server/ready.c)
	target_link_libraries(test-server-ready PRIVATE mh_server_session)
	mh_c89(test-server-ready)
	add_test(NAME server-readiness COMMAND test-server-ready)

	add_executable(test-server-grid tests/server/grid.c)
	target_link_libraries(test-server-grid PRIVATE mh_server_session)
	mh_c89(test-server-grid)
	add_test(NAME server-grid COMMAND test-server-grid)

	add_executable(test-server-setup tests/server/setup.c)
	target_link_libraries(test-server-setup PRIVATE mh_server_session)
	mh_c89(test-server-setup)
	add_test(NAME server-race-setup COMMAND test-server-setup)

	add_executable(test-server-commands tests/server/commands.c)
	target_link_libraries(test-server-commands PRIVATE mh_server_session)
	mh_c89(test-server-commands)
	add_test(NAME server-client-commands COMMAND test-server-commands)

	# Link the whole application through explicit SDK-free services only.
	if(MH_DEBUG AND "software" IN_LIST MH_RENDER_PROFILES)
		add_executable(test-portable-host
			tests/integration/portable_host.c
			tests/support/wireframe_source.c
			tests/support/resource_pack.c
			tests/support/software_presentation.c)
		target_link_libraries(test-portable-host PRIVATE
			mh_menu_layout_dta
			mh_palette_act
			mh_catalog_fallback
			mh_horn_fallback
			mh_material_mat_srf
			mh_environment_esd
			mh_wireframe_lob
			mh_collision_fallback
			mh_model_myo
			mh_world_myw
			mh_atlas_remap
			mh_navigation_mot_dat
			mh_animation_lightwave
			mh_font_fallback
			mh_sprite_spr
			mh_image_fallback
			mh_image_pdi
			mh_menus_app
			mh_render_software
			mh_resource_memory
			mh_user_data_memory)
		mh_c89(test-portable-host)
		add_test(NAME portable-application-host COMMAND
			test-portable-host)
	endif()

	add_executable(test-renderer tests/unit/test_renderer.c)
	target_link_libraries(test-renderer PRIVATE mh_renderer)
	mh_c89(test-renderer)
	add_test(NAME renderer-dispatch COMMAND test-renderer)

	add_executable(test-error tests/unit/test_error.c)
	target_link_libraries(test-error PRIVATE mh_error)
	mh_c89(test-error)
	add_test(NAME error-values COMMAND test-error)

	add_executable(test-paused-clock tests/unit/test_paused_clock.c)
	target_link_libraries(test-paused-clock PRIVATE mh_clock_source)
	mh_c89(test-paused-clock)
	add_test(NAME paused-clock COMMAND test-paused-clock)

	add_executable(test-storage tests/unit/test_storage.c)
	target_link_libraries(test-storage PRIVATE mh_storage_filesystem)
	mh_c89(test-storage)
	add_test(NAME storage-lifecycle COMMAND test-storage)

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		add_executable(test-directory-sync tests/storage/posix_sync.c)
		target_link_libraries(test-directory-sync PRIVATE
			mh_storage_filesystem mh_user_data_filesystem)
		target_link_options(test-directory-sync PRIVATE "-Wl,--wrap=fsync")
		mh_c89(test-directory-sync)
		add_test(NAME directory-sync COMMAND test-directory-sync)
	endif()

	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resource-fixture")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/data"
		"fixture")
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/empty" "")

	add_executable(test-resource tests/unit/test_resource.c)
	target_link_libraries(test-resource PRIVATE mh_resource_filesystem)
	mh_c89(test-resource)
	add_test(NAME resource-provider COMMAND test-resource
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/data"
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/empty"
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/missing")

	# Exercise stdio resource reads without optional directory operations.
	add_executable(test-resource-basic
		tests/unit/test_resource.c
		src/backends/storage/filesystem_resource.c)
	target_link_libraries(test-resource-basic PRIVATE
		mh_resource_source
		mh_filesystem_io)
	mh_c89(test-resource-basic)
	add_test(NAME resource-provider-basic COMMAND test-resource-basic
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/data"
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/empty"
		"${CMAKE_CURRENT_BINARY_DIR}/resource-fixture/missing")

	if(WIN32)
		add_executable(test-windows-resource
			tests/storage/windows_resource.c)
		target_link_libraries(test-windows-resource PRIVATE
			mh_resource_filesystem
			mh_file_read_filesystem
			mh_filesystem_platform)
		mh_c89(test-windows-resource)
		add_test(NAME windows-resource COMMAND test-windows-resource)

		add_executable(test-windows-data tests/storage/windows_data.c)
		target_link_libraries(test-windows-data PRIVATE
			mh_user_data_filesystem
			mh_resource_filesystem)

		if(MINGW)
			target_sources(test-windows-data PRIVATE
				tests/support/windows_reopen.c)
			target_link_options(test-windows-data PRIVATE
				"-Wl,--wrap=__imp_ReOpenFile")
		endif()
		mh_c89(test-windows-data)
		add_test(NAME windows-data COMMAND test-windows-data)
		add_test(NAME windows-data-relocation COMMAND test-windows-data
			--relocation)
		set_tests_properties(windows-data-relocation PROPERTIES
			SKIP_RETURN_CODE 77)
	endif()

	add_executable(test-horn-source
		tests/unit/test_horn_source.c
		tests/support/horn_source.c)
	target_link_libraries(test-horn-source PRIVATE
		mh_horn_loading
		mh_resource_memory)
	mh_c89(test-horn-source)
	add_test(NAME horn-source COMMAND test-horn-source)

	add_executable(test-menu-tables
		tests/unit/test_menu_tables.c
		tests/support/menu_tables.c)
	target_link_libraries(test-menu-tables PRIVATE
		mh_palette_loading
		mh_menu_layout_loading
		mh_resource_memory)
	mh_c89(test-menu-tables)
	add_test(NAME menu-table-sources COMMAND test-menu-tables)

	add_executable(test-profile-source
		tests/unit/test_profile_source.c
		tests/support/profile_source.c)
	target_link_libraries(test-profile-source PRIVATE
		mh_profile_loading
		mh_resource_memory)
	mh_c89(test-profile-source)
	add_test(NAME profile-source COMMAND test-profile-source)

	add_executable(test-profile-data tests/unit/test_profile_data.c)
	target_link_libraries(test-profile-data PRIVATE mh_catalog_data mh_horn)
	mh_c89(test-profile-data)
	add_test(NAME profile-data COMMAND test-profile-data)

	add_executable(test-surface-source
		tests/unit/test_surface_source.c
		tests/support/surface_source.c)
	target_link_libraries(test-surface-source PRIVATE
		mh_material_loading
		mh_environment_loading
		mh_resource_memory)
	mh_c89(test-surface-source)
	add_test(NAME surface-source COMMAND test-surface-source)

	add_executable(test-catalog-source
		tests/unit/test_catalog_source.c
		tests/support/catalog_source.c
		tests/support/horn_source.c)
	target_link_libraries(test-catalog-source PRIVATE
		mh_catalog_loading
		mh_settings_store
		mh_resource_memory
		mh_user_data_memory)
	mh_c89(test-catalog-source)
	add_test(NAME catalog-source COMMAND test-catalog-source)

	add_executable(test-content-catalog tests/unit/test_content_catalog.c)
	target_link_libraries(test-content-catalog PRIVATE
		mh_horn_fallback
		mh_catalog_fallback
		mh_catalog_loading
		mh_resource_memory
		mh_user_data_memory
		mh_settings_store)
	mh_c89(test-content-catalog)
	add_test(NAME content-catalog COMMAND test-content-catalog)

	add_executable(test-settings-store tests/unit/test_settings_store.c)
	target_link_libraries(test-settings-store PRIVATE
		mh_horn_fallback
		mh_catalog_fallback
		mh_settings_store
		mh_resource_memory
		mh_user_data_memory)
	mh_c89(test-settings-store)
	add_test(NAME settings-store-providers COMMAND test-settings-store)

	add_executable(test-user-data tests/unit/test_user_data.c)
	target_link_libraries(test-user-data PRIVATE
		mh_user_data_memory
		mh_settings_storage
		mh_storage)
	mh_c89(test-user-data)
	add_test(NAME user-data-provider COMMAND test-user-data)

	add_executable(test-address-provider tests/unit/test_address_provider.c)
	target_link_libraries(test-address-provider PRIVATE
		mh_address_store
		mh_user_data_memory)
	mh_c89(test-address-provider)
	add_test(NAME tcp-address-provider COMMAND test-address-provider)

	add_executable(test-data-file tests/unit/test_data_file.c)
	target_link_libraries(test-data-file PRIVATE mh_user_data_memory)
	mh_c89(test-data-file)
	add_test(NAME stored-file-provider COMMAND test-data-file)

	if(UNIX AND TARGET mh_user_data_filesystem)
		add_executable(test-data-file-native
			tests/unit/test_data_file.c)
		target_compile_definitions(test-data-file-native PRIVATE
			MH_HAVE_USER_DATA_FILES=1)
		target_link_libraries(test-data-file-native PRIVATE
			mh_user_data_filesystem)
		mh_c89(test-data-file-native)
		add_test(NAME stored-file-native COMMAND test-data-file-native)
	endif()

	add_executable(test-directory-browse tests/unit/test_directory_browse.c)
	target_link_libraries(test-directory-browse PRIVATE mh_user_data_memory)
	mh_c89(test-directory-browse)
	add_test(NAME directory-browse-provider COMMAND test-directory-browse)

	if(UNIX AND TARGET mh_user_data_filesystem)
		add_executable(test-directory-browse-native
			tests/unit/test_directory_browse.c)
		target_compile_definitions(test-directory-browse-native PRIVATE
			MH_HAVE_USER_DATA_FILES=1)
		target_link_libraries(test-directory-browse-native PRIVATE
			mh_resource_filesystem
			mh_user_data_filesystem)
		mh_c89(test-directory-browse-native)
		add_test(NAME directory-browse-native COMMAND
			test-directory-browse-native)
	endif()

	add_executable(test-user-directories tests/unit/test_user_directories.c)
	target_link_libraries(test-user-directories PRIVATE mh_user_data_memory)
	mh_c89(test-user-directories)
	add_test(NAME user-data-directories COMMAND test-user-directories)

	if(UNIX AND TARGET mh_user_data_filesystem)
		add_executable(test-user-directories-native
			tests/unit/test_user_directories.c)
		target_compile_definitions(test-user-directories-native PRIVATE
			MH_HAVE_USER_DATA_FILES=1)
		target_link_libraries(test-user-directories-native PRIVATE
			mh_user_data_filesystem)
		mh_c89(test-user-directories-native)
		add_test(NAME user-data-directories-native COMMAND
			test-user-directories-native)
	endif()

	add_executable(test-highscore-provider
		tests/unit/test_highscore_provider.c)
	target_link_libraries(test-highscore-provider PRIVATE
		mh_highscore_storage
		mh_user_data_memory)
	mh_c89(test-highscore-provider)
	add_test(NAME high-score-provider COMMAND test-highscore-provider)

	add_executable(test-record-provider tests/unit/test_record_provider.c)
	target_link_libraries(test-record-provider PRIVATE
		mh_record_storage
		mh_user_data_memory)
	mh_c89(test-record-provider)
	add_test(NAME record-provider COMMAND test-record-provider)

	add_executable(test-race-record-provider
		tests/unit/test_race_record_provider.c)
	target_link_libraries(test-race-record-provider PRIVATE
		mh_race_records
		mh_user_data_memory)
	mh_c89(test-race-record-provider)
	add_test(NAME race-record-provider COMMAND test-race-record-provider)

	add_executable(test-league-provider tests/unit/test_league_provider.c)
	target_link_libraries(test-league-provider PRIVATE
		mh_leagues
		mh_user_data_memory)
	mh_c89(test-league-provider)
	add_test(NAME league-provider COMMAND test-league-provider)

	add_executable(test-progress-provider
		tests/unit/test_progress_provider.c)
	target_link_libraries(test-progress-provider PRIVATE
		mh_catalog_fallback
		mh_progress
		mh_user_data_memory
		mh_resource_memory)
	mh_c89(test-progress-provider)
	add_test(NAME progress-provider COMMAND test-progress-provider)

	add_executable(test-track-flag-provider
		tests/unit/test_track_flag_provider.c)
	target_link_libraries(test-track-flag-provider PRIVATE
		mh_track_flag_storage
		mh_user_data_memory
		mh_resource_memory)
	mh_c89(test-track-flag-provider)
	add_test(NAME track-flag-provider COMMAND test-track-flag-provider)

	add_executable(test-control-profiles tests/unit/test_control_profiles.c)
	target_link_libraries(test-control-profiles PRIVATE
		mh_control_profiles
		mh_resource_memory
		mh_user_data_memory)
	mh_c89(test-control-profiles)
	add_test(NAME control-profile-selection COMMAND test-control-profiles)

	add_executable(test-control-provider tests/unit/test_control_provider.c)
	target_link_libraries(test-control-provider PRIVATE
		mh_control_store
		mh_resource_memory
		mh_user_data_memory)
	mh_c89(test-control-provider)
	add_test(NAME control-profile-provider COMMAND test-control-provider)

	add_executable(test-resource-layers tests/unit/test_resource_layers.c)
	target_link_libraries(test-resource-layers PRIVATE
		mh_content
		mh_resource_memory)
	mh_c89(test-resource-layers)
	add_test(NAME resource-layers COMMAND test-resource-layers)

	if(NOT EMSCRIPTEN)
		add_executable(test-package-layers
			tests/assets/package_layers.c)
		target_link_libraries(test-package-layers PRIVATE
			mh_content
			mh_resource_filesystem
			mh_resource_zip)
		mh_c89(test-package-layers)
	endif()

	add_executable(test-package tests/unit/test_package.c)
	target_link_libraries(test-package PRIVATE
		mh_resource_zip
		mh_resource_memory
		mh_user_data_memory
		mh_zip_write
		mh_vfs
		mh_core)
	mh_c89(test-package)
	add_test(NAME asset-package COMMAND test-package)

	add_executable(test-zip-compression tests/unit/test_zip_compression.c)
	target_link_libraries(test-zip-compression PRIVATE
		mh_resource_zip
		mh_resource_memory
		mh_user_data_memory
		mh_zip_write
		mh_core)
	mh_c89(test-zip-compression)
	add_test(NAME zip-compression-writer COMMAND test-zip-compression)

	if(TARGET mh_resource_filesystem)
		add_executable(mh-zip-compression-check
			tests/integration/zip_compression.c)
		target_link_libraries(mh-zip-compression-check PRIVATE
			mh_resource_zip
			mh_resource_filesystem)
		mh_c89(mh-zip-compression-check)

		if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
			add_test(NAME zip-compression COMMAND
				"${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/assets/check_zip_compression.py"
				--reader $<TARGET_FILE:mh-zip-compression-check>)
			set_tests_properties(zip-compression PROPERTIES TIMEOUT
				60)
		endif()
	endif()

	add_executable(test-resource-source tests/unit/test_resource_source.c)
	target_link_libraries(test-resource-source PRIVATE
		mh_catalog_fallback
		mh_horn_fallback
		mh_resource_memory
		mh_asset_loaders)
	mh_c89(test-resource-source)
	add_test(NAME resource-source COMMAND test-resource-source)

	add_executable(test-music-source tests/unit/test_music_source.c)
	target_link_libraries(test-music-source PRIVATE
		mh_audio_fallback
		mh_resource_memory
		mh_audio_assets)
	mh_c89(test-music-source)
	add_test(NAME music-resource-source COMMAND test-music-source)

	add_executable(mh-resource-loaders
		tests/assets/resource_loaders.c
		tests/support/retro_source.c
		tests/support/profile_source.c
		tests/support/horn_source.c
		tests/support/surface_source.c
		tests/support/catalog_source.c
		tests/support/collision_source.c
		tests/support/geometry_source.c
		tests/support/atlas_source.c
		tests/support/animation_source.c
		tests/support/navigation_source.c)
	target_link_libraries(mh-resource-loaders PRIVATE
		mh_retro_fallback
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
		mh_font_fallback
		mh_sprite_spr
		mh_audio_fallback
		mh_image_fallback
		mh_image_pdi
		mh_resource_memory
		mh_resource_filesystem
		mh_asset_loaders
		mh_audio_assets
		mh_race_scene
		mh_ui_assets)
	mh_c89(mh-resource-loaders)

	add_executable(test-text tests/unit/test_text.c)
	target_link_libraries(test-text PRIVATE mh_text)
	mh_c89(test-text)
	add_test(NAME ascii-text COMMAND test-text)

	add_executable(test-charset tests/unit/test_charset.c)
	target_link_libraries(test-charset PRIVATE mh_text)
	mh_c89(test-charset)
	add_test(NAME character-conversion COMMAND test-charset)

	add_executable(test-demo tests/unit/test_demo.c)
	target_link_libraries(test-demo PRIVATE mh_assets)
	mh_c89(test-demo)
	add_test(NAME demo-recording COMMAND test-demo)

	add_executable(test-demo-record tests/unit/test_demo_record.c)
	target_link_libraries(test-demo-record PRIVATE mh_demo)
	mh_c89(test-demo-record)
	add_test(NAME demo-writing COMMAND test-demo-record)

	add_executable(test-demo-sample tests/unit/test_demo_sample.c)
	target_link_libraries(test-demo-sample PRIVATE mh_demo)
	mh_c89(test-demo-sample)
	add_test(NAME demo-interpolation COMMAND test-demo-sample)

	add_executable(test-demo-playback tests/unit/test_demo_playback.c)
	target_link_libraries(test-demo-playback PRIVATE
		mh_file_read_filesystem
		mh_demo)
	mh_c89(test-demo-playback)
	add_test(NAME demo-playback COMMAND test-demo-playback)

	add_executable(test-demo-benchmark tests/unit/test_demo_benchmark.c)
	target_link_libraries(test-demo-benchmark PRIVATE mh_demo)
	mh_c89(test-demo-benchmark)
	add_test(NAME demo-benchmark COMMAND test-demo-benchmark)

	add_executable(test-demo-progress tests/unit/test_demo_progress.c)
	target_link_libraries(test-demo-progress PRIVATE mh_demo)
	mh_c89(test-demo-progress)
	add_test(NAME demo-recorded-progress COMMAND test-demo-progress)

	add_executable(test-demo-race tests/unit/test_demo_race.c)
	target_link_libraries(test-demo-race PRIVATE mh_demo_sim)
	mh_c89(test-demo-race)
	add_test(NAME demo-race-field COMMAND test-demo-race)

	add_executable(test-demo-capture tests/unit/test_demo_capture.c)
	target_link_libraries(test-demo-capture PRIVATE mh_demo_sim)
	mh_c89(test-demo-capture)
	add_test(NAME demo-scene-capture COMMAND test-demo-capture)

	add_executable(test-ghost-capture tests/unit/test_ghost_capture.c)
	target_link_libraries(test-ghost-capture PRIVATE mh_demo_sim)
	mh_c89(test-ghost-capture)
	add_test(NAME ghost-scene-capture COMMAND test-ghost-capture)

	add_executable(test-network-capture tests/unit/test_network_capture.c)
	target_link_libraries(test-network-capture PRIVATE mh_demo_sim)
	mh_c89(test-network-capture)
	add_test(NAME network-scene-capture COMMAND test-network-capture)

	add_executable(test-clipboard tests/unit/test_clipboard.c)
	target_link_libraries(test-clipboard PRIVATE mh_input)
	mh_c89(test-clipboard)
	add_test(NAME clipboard-provider COMMAND test-clipboard)

	add_executable(test-input-provider tests/unit/test_input_provider.c)
	target_link_libraries(test-input-provider PRIVATE mh_input)
	mh_c89(test-input-provider)
	add_test(NAME input-provider COMMAND test-input-provider)

	add_executable(test-canvas tests/unit/test_canvas.c)
	target_link_libraries(test-canvas PRIVATE mh_canvas mh_window_system)
	mh_c89(test-canvas)
	add_test(NAME canvas-provider COMMAND test-canvas)

	add_executable(test-window-system tests/unit/test_window_system.c)
	target_link_libraries(test-window-system PRIVATE mh_graphics_catalog)
	mh_c89(test-window-system)
	add_test(NAME window-system-provider COMMAND test-window-system)

	add_executable(test-image-collections
		tests/unit/test_image_collections.c)
	target_link_libraries(test-image-collections PRIVATE
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_catalog_fallback
		mh_atlas_remap
		mh_navigation_mot_dat
		mh_animation_lightwave
		mh_resource_memory
		mh_race_scene)
	mh_c89(test-image-collections)
	add_test(NAME image-collection-provider COMMAND test-image-collections)

	add_executable(test-scene-images tests/unit/test_scene_images.c)
	target_link_libraries(test-scene-images PRIVATE
		mh_resource_memory
		mh_race_scene)
	mh_c89(test-scene-images)
	add_test(NAME scene-image-provider COMMAND test-scene-images)

	add_executable(test-results-image tests/unit/test_results_image.c)
	target_link_libraries(test-results-image PRIVATE
		mh_resource_memory
		mh_ui_assets)
	mh_c89(test-results-image)
	add_test(NAME results-image-provider COMMAND test-results-image)

	add_executable(test-menu-sprite-source
		tests/unit/test_menu_sprite_source.c)
	target_link_libraries(test-menu-sprite-source PRIVATE
		mh_sprite_collections
		mh_ui
		mh_resource_memory)
	mh_c89(test-menu-sprite-source)
	add_test(NAME menu-sprite-source-provider COMMAND
		test-menu-sprite-source)

	add_executable(test-sprite-source tests/unit/test_sprite_source.c)
	target_link_libraries(test-sprite-source PRIVATE
		mh_sprite_collections
		mh_ui_assets
		mh_resource_memory)
	mh_c89(test-sprite-source)
	add_test(NAME sprite-source-provider COMMAND test-sprite-source)

	add_executable(test-font-source tests/unit/test_font_source.c)
	target_link_libraries(test-font-source PRIVATE
		mh_font_loading
		mh_resource_memory
		mh_race_scene)
	mh_c89(test-font-source)
	add_test(NAME font-source-provider COMMAND test-font-source)

	add_executable(test-icon tests/unit/test_icon.c)
	target_link_libraries(test-icon PRIVATE mh_icon mh_resource_memory)
	mh_c89(test-icon)
	add_test(NAME window-icon-assets COMMAND test-icon)

	add_executable(test-image-decoder tests/unit/test_image_decoder.c)
	target_link_libraries(test-image-decoder PRIVATE
		mh_image_fallback
		mh_image_loading
		mh_resource_memory
		mh_ui_assets)
	mh_c89(test-image-decoder)
	add_test(NAME image-decoder-provider COMMAND test-image-decoder)

	add_executable(test-image-encoder tests/unit/test_image_encoder.c)
	target_link_libraries(test-image-encoder PRIVATE mh_image_tga mh_input)
	mh_c89(test-image-encoder)
	add_test(NAME image-encoder COMMAND test-image-encoder)

	add_executable(test-picture-paths tests/unit/test_picture_paths.c)
	target_link_libraries(test-picture-paths PRIVATE
		mh_picture_storage
		mh_user_data_memory)
	mh_c89(test-picture-paths)
	add_test(NAME screenshot-path-providers COMMAND test-picture-paths)

	add_executable(test-picture-provider tests/unit/test_picture_provider.c)
	target_link_libraries(test-picture-provider PRIVATE
		mh_picture_storage
		mh_user_data_memory
		mh_input)
	mh_c89(test-picture-provider)
	add_test(NAME screenshot-provider COMMAND test-picture-provider)

	add_executable(test-recording-paths tests/unit/test_recording_paths.c)
	target_link_libraries(test-recording-paths PRIVATE
		mh_recording_paths
		mh_user_data_memory)
	mh_c89(test-recording-paths)
	add_test(NAME recording-path-providers COMMAND test-recording-paths)

	add_executable(test-recording-catalog-provider
		tests/unit/test_recording_catalog_provider.c)
	target_link_libraries(test-recording-catalog-provider PRIVATE
		mh_recording_catalog
		mh_user_data_memory
		mh_resource_memory)
	mh_c89(test-recording-catalog-provider)
	add_test(NAME recording-catalog-provider COMMAND
		test-recording-catalog-provider)

	add_executable(test-demo-provider tests/unit/test_demo_provider.c)
	target_link_libraries(test-demo-provider PRIVATE
		mh_demo_storage
		mh_user_data_memory)
	mh_c89(test-demo-provider)
	add_test(NAME demo-storage-provider COMMAND test-demo-provider)

	if(UNIX)
		add_executable(test-demo-files tests/unit/test_demo_files.c)
		target_link_libraries(test-demo-files PRIVATE
			mh_file_read_filesystem
			mh_demo_files)
		mh_c89(test-demo-files)
		add_test(NAME demo-file-storage COMMAND test-demo-files)

		add_executable(test-recording-save
			tests/unit/test_recording_save.c)
		target_link_libraries(test-recording-save PRIVATE
			mh_file_read_filesystem
			mh_demo_files)
		mh_c89(test-recording-save)
		add_test(NAME recording-save-storage COMMAND
			test-recording-save)

		add_executable(test-recording-catalog
			tests/unit/test_recording_catalog.c)
		target_link_libraries(test-recording-catalog PRIVATE
			mh_recording_catalog_files)
		mh_c89(test-recording-catalog)
		add_test(NAME recording-catalog-storage COMMAND
			test-recording-catalog)
	endif()

	add_executable(test-ai-personality tests/unit/test_ai_personality.c)
	target_link_libraries(test-ai-personality PRIVATE mh_sim)
	mh_c89(test-ai-personality)
	add_test(NAME ai-personality COMMAND test-ai-personality)

	add_executable(test-race-tuning tests/unit/test_race_tuning.c)
	target_link_libraries(test-race-tuning PRIVATE mh_sim)
	mh_c89(test-race-tuning)
	add_test(NAME race-performance-tuning COMMAND test-race-tuning)

	add_executable(test-field-tuning tests/unit/test_field_tuning.c)
	target_link_libraries(test-field-tuning PRIVATE mh_sim)
	mh_c89(test-field-tuning)
	add_test(NAME field-performance-tuning COMMAND test-field-tuning)

	add_executable(test-contact-schedule tests/unit/test_contact_schedule.c)
	target_link_libraries(test-contact-schedule PRIVATE mh_sim)
	mh_c89(test-contact-schedule)
	add_test(NAME car-contact-schedule COMMAND test-contact-schedule)

	add_executable(test-body-points tests/unit/test_body_points.c)
	target_link_libraries(test-body-points PRIVATE mh_sim)
	mh_c89(test-body-points)
	add_test(NAME car-body-points COMMAND test-body-points)

	add_executable(test-body-frame tests/unit/test_body_frame.c)
	target_link_libraries(test-body-frame PRIVATE mh_sim)
	mh_c89(test-body-frame)
	add_test(NAME body-frame-feedback COMMAND test-body-frame)

	add_executable(test-finish-controls tests/unit/test_finish_controls.c)
	target_link_libraries(test-finish-controls PRIVATE mh_sim)
	mh_c89(test-finish-controls)
	add_test(NAME finished-car-controls COMMAND test-finish-controls)

	add_executable(test-movie tests/unit/test_video.c)
	target_link_libraries(test-movie PRIVATE mh_sim mh_video_dispatch)
	mh_c89(test-movie)
	add_test(NAME video-index COMMAND test-movie)

	add_executable(test-opening-movies tests/unit/test_opening_videos.c)
	target_link_libraries(test-opening-movies PRIVATE mh_ui)
	mh_c89(test-opening-movies)
	add_test(NAME opening-videos COMMAND test-opening-movies)

	if(TARGET mh_transport_udp)
		if(TARGET mh_clock_system)
			add_executable(test-udp-socket
				tests/integration/udp_socket.c)
			target_link_libraries(test-udp-socket PRIVATE
				mh_transport_udp
				mh_clock_system)
			mh_c89(test-udp-socket)
			add_test(NAME udp-native-socket COMMAND
				test-udp-socket)
			set_tests_properties(udp-native-socket PROPERTIES
				TIMEOUT 15)
		endif()

		# Production LAN/INET delivery over deterministic test datagrams.
		add_library(mh_test_transport_udp STATIC
			tests/support/udp_virtual_io.c
			src/backends/network/udp_transport.c
			src/backends/network/udp_address.c
			src/backends/network/udp_wait.c
			src/backends/network/udp_multiplex.c
			src/backends/network/udp_target.c
			src/backends/network/udp_connection.c
			src/backends/network/udp_protocol.c
			src/backends/network/udp_discovery.c)

		foreach(protocol IN LISTS MH_NETWORK_PROTOCOLS)
			target_sources(mh_test_transport_udp PRIVATE
				src/backends/network/udp_${protocol}.c)
		endforeach()

		target_link_libraries(mh_test_transport_udp PUBLIC
			mh_udp_challenge
			mh_network_build
			mh_udp_description
			mh_reliable_delivery
			mh_network_session
			mh_clock_source
			mh_resolver)
		mh_c89(mh_test_transport_udp)

		add_executable(test-network-session tests/unit/test_network.c)
		target_link_libraries(test-network-session PRIVATE
			mh_test_transport_udp)
		mh_c89(test-network-session)
		mh_protocol_test("legacy" network-session-io test-network-session)
		mh_protocol_test("legacy;openmh" network-session-shared-port
			test-network-session --shared-port)
		mh_protocol_test("openmh" network-session-openmh
			test-network-session --openmh)
		foreach(test network-session-io network-session-shared-port
			network-session-openmh)
			if(TEST ${test})
				set_tests_properties(${test} PROPERTIES TIMEOUT 90)
			endif()
		endforeach()
	endif()

	if(MH_BUILD_CLIENT AND TARGET mh_test_transport_udp)
		add_library(mh_test_network_race OBJECT
			src/client/session/network_race.c)
		target_link_libraries(mh_test_network_race PRIVATE
			mh_network_setup
			mh_demo_recording)
		mh_c89(mh_test_network_race)

		add_executable(mh-net-play-check
			tests/integration/network_play.c
			$<TARGET_OBJECTS:mh_test_network_race>)
		target_link_libraries(mh-net-play-check PRIVATE
			mh_network_session
			mh_network_setup
			mh_diagnostic_runtime
			mh_test_transport_udp
			mh_lobby_ui
			mh_demo_recording)
		mh_c89(mh-net-play-check)

		add_executable(test-network-message-clock
			tests/integration/network_message_clock.c
			$<TARGET_OBJECTS:mh_test_network_race>)
		target_link_libraries(test-network-message-clock PRIVATE
			mh_network_session
			mh_network_setup
			mh_test_transport_udp
			mh_demo_recording)
		mh_c89(test-network-message-clock)
		mh_protocol_test("legacy" network-message-clock
			test-network-message-clock)

		add_executable(test-network-progress
			tests/integration/network_progress.c
			$<TARGET_OBJECTS:mh_test_network_race>)
		target_link_libraries(test-network-progress PRIVATE
			mh_network_session
			mh_network_setup
			mh_test_transport_udp
			mh_demo_recording)
		mh_c89(test-network-progress)
		mh_protocol_test("legacy" network-progress test-network-progress)

		add_executable(test-network-return
			tests/integration/network_return.c
			$<TARGET_OBJECTS:mh_test_network_race>)
		target_link_libraries(test-network-return PRIVATE
			mh_network_session
			mh_network_setup
			mh_test_transport_udp
			mh_demo_recording)
		mh_c89(test-network-return)
		mh_protocol_test("legacy" network-return test-network-return)
	endif()

	add_executable(test-reliable-delivery
		tests/unit/test_reliable_delivery.c)
	target_link_libraries(test-reliable-delivery PRIVATE mh_reliable_delivery)
	mh_c89(test-reliable-delivery)
	add_test(NAME reliable-delivery COMMAND test-reliable-delivery)

	add_executable(test-udp-control tests/unit/test_udp_control.c)
	target_link_libraries(test-udp-control PRIVATE mh_udp_framing)
	mh_c89(test-udp-control)
	add_test(NAME udp-control-codec COMMAND test-udp-control)

	add_executable(test-udp-description
		tests/unit/test_udp_description.c)
	target_link_libraries(test-udp-description PRIVATE mh_udp_description)
	mh_c89(test-udp-description)
	add_test(NAME udp-session-description COMMAND
		test-udp-description)

	if(TARGET mh_transport_udp AND (TARGET mh_resolver_posix OR
		TARGET mh_resolver_windows))

		if(TARGET mh_resolver_windows)
			set(mh_test_resolver mh_resolver_windows)
		else()
			set(mh_test_resolver mh_resolver_posix)
		endif()

		if(TARGET mh_clock_system)
			add_executable(test-network-resolution
				tests/integration/network_resolution.c)
			target_link_libraries(test-network-resolution PRIVATE
				mh_transport_udp
				${mh_test_resolver}
				mh_clock_system)
			mh_c89(test-network-resolution)
			mh_protocol_test("legacy" network-resolution
				test-network-resolution)
			if(TEST network-resolution)
				set_tests_properties(network-resolution PROPERTIES
					TIMEOUT 30)
			endif()
		endif()

		add_executable(test-udp-lobby
			tests/integration/udp_lobby.c)
		target_link_libraries(test-udp-lobby PRIVATE
			mh_network_session
			mh_transport_udp
			${mh_test_resolver})
		mh_c89(test-udp-lobby)
		mh_protocol_test("legacy" udp-lobby test-udp-lobby)
		if(TEST udp-lobby)
			set_tests_properties(udp-lobby PROPERTIES TIMEOUT 30)
		endif()

		if(WIN32)
			add_library(mh_test_stdin STATIC
				tests/support/stdin_windows.c)
		else()
			add_library(mh_test_stdin STATIC
				tests/support/stdin_posix.c)
		endif()
		mh_c89(mh_test_stdin)

		add_executable(mh-dedicated-client-check
			tests/integration/dedicated_client.c)
		target_link_libraries(mh-dedicated-client-check PRIVATE
			mh_network_session
			mh_transport_udp
			${mh_test_resolver}
			mh_test_stdin
			mh_clock_system
			mh_lobby_ui)
		mh_c89(mh-dedicated-client-check)

		add_executable(mh-dedicated-race-check
			tests/integration/dedicated_races.c)
		target_link_libraries(mh-dedicated-race-check PRIVATE
			mh_network_session
			mh_transport_udp
			${mh_test_resolver}
			mh_test_stdin
			mh_clock_system
			mh_resource_filesystem
			mh_server_config_load
			mh_server_courses
			mh_navigation_mot
			mh_route_geometry)
		mh_c89(mh-dedicated-race-check)
	endif()

	if(TARGET mh_transport_udp AND TARGET mh_resolver_posix)
		add_executable(test-udp-transport
			tests/integration/udp_transport.c)
		target_link_libraries(test-udp-transport PRIVATE
			mh_transport_udp
			mh_resolver_posix
			mh_udp_description)
		mh_c89(test-udp-transport)
		add_test(NAME udp-transport COMMAND
			test-udp-transport)
		set_tests_properties(udp-transport PROPERTIES TIMEOUT 30)
	endif()

	add_executable(test-discovery tests/unit/test_discovery.c)
	target_link_libraries(test-discovery PRIVATE mh_net)
	mh_c89(test-discovery)
	add_test(NAME native-session-discovery COMMAND test-discovery)

	add_executable(test-net-session tests/unit/test_net_session.c)
	target_link_libraries(test-net-session PRIVATE mh_lobby_ui)
	mh_c89(test-net-session)
	add_test(NAME network-session COMMAND test-net-session)

	add_executable(test-patched-lobby-client
		tests/integration/patched_lobby_client.c)
	target_link_libraries(test-patched-lobby-client PRIVATE mh_net)
	mh_c89(test-patched-lobby-client)
	mh_protocol_test("legacy" patched-lobby-client test-patched-lobby-client)

	add_executable(test-dedicated-statistics
		tests/integration/dedicated_statistics.c)
	target_link_libraries(test-dedicated-statistics PRIVATE mh_net)
	mh_c89(test-dedicated-statistics)
	mh_protocol_test("openmh" dedicated-statistics test-dedicated-statistics)

	add_executable(test-dedicated-race-client
		tests/integration/dedicated_race_client.c)
	target_link_libraries(test-dedicated-race-client PRIVATE mh_net)
	mh_c89(test-dedicated-race-client)
	mh_protocol_test("openmh" dedicated-race-client test-dedicated-race-client)

	add_executable(test-dedicated-setup tests/integration/dedicated_setup.c)
	target_link_libraries(test-dedicated-setup PRIVATE
		mh_net_sim
		mh_server_session)
	mh_c89(test-dedicated-setup)
	mh_protocol_test("openmh" dedicated-client-setup test-dedicated-setup)

	add_executable(test-net-car-catalog tests/unit/test_net_car_catalog.c)
	target_link_libraries(test-net-car-catalog PRIVATE mh_net)
	mh_c89(test-net-car-catalog)
	add_test(NAME network-admission-car-catalog COMMAND
		test-net-car-catalog)

	add_executable(test-net-vehicle tests/unit/test_net_vehicle.c)
	target_link_libraries(test-net-vehicle PRIVATE mh_net)
	mh_c89(test-net-vehicle)
	add_test(NAME network-vehicle-encoding COMMAND test-net-vehicle)

	add_executable(test-net-history tests/unit/test_net_history.c)
	target_link_libraries(test-net-history PRIVATE mh_net)
	mh_c89(test-net-history)
	add_test(NAME network-snapshot-history COMMAND test-net-history)

	add_executable(test-net-start tests/unit/test_net_start.c)
	target_link_libraries(test-net-start PRIVATE mh_net)
	mh_c89(test-net-start)
	add_test(NAME network-start-barrier COMMAND test-net-start)

	add_executable(test-net-finish tests/unit/test_net_finish.c)
	target_link_libraries(test-net-finish PRIVATE mh_net)
	mh_c89(test-net-finish)
	add_test(NAME network-finish COMMAND test-net-finish)

	add_executable(test-net-elimination tests/unit/test_net_elimination.c)
	target_link_libraries(test-net-elimination PRIVATE mh_net)
	mh_c89(test-net-elimination)
	add_test(NAME network-elimination COMMAND test-net-elimination)

	add_executable(test-net-pose tests/unit/test_net_pose.c)
	target_link_libraries(test-net-pose PRIVATE mh_net)
	mh_c89(test-net-pose)
	add_test(NAME network-pose COMMAND test-net-pose)

	add_executable(test-net-drive tests/unit/test_net_drive.c)
	target_link_libraries(test-net-drive PRIVATE mh_scene_models mh_net_sim)
	mh_c89(test-net-drive)
	add_test(NAME network-driving-bridge COMMAND test-net-drive)

	add_executable(test-net-race tests/unit/test_net_race.c)
	target_link_libraries(test-net-race PRIVATE mh_net_sim)
	mh_c89(test-net-race)
	mh_protocol_test("legacy" network-race-field test-net-race)

	add_executable(test-skids tests/unit/test_skids.c)
	target_link_libraries(test-skids PRIVATE mh_effect_mesh)
	mh_c89(test-skids)
	add_test(NAME skid-marks COMMAND test-skids)

	add_executable(test-particles tests/unit/test_particles.c)
	target_link_libraries(test-particles PRIVATE mh_sim)
	mh_c89(test-particles)
	add_test(NAME particles COMMAND test-particles)

	add_executable(test-particle-mesh tests/unit/test_particle_mesh.c)
	target_link_libraries(test-particle-mesh PRIVATE mh_effect_mesh)
	mh_c89(test-particle-mesh)
	add_test(NAME particle-mesh COMMAND test-particle-mesh)

	add_executable(test-camera tests/unit/test_camera.c)
	target_link_libraries(test-camera PRIVATE mh_sim)
	mh_c89(test-camera)
	add_test(NAME race-camera COMMAND test-camera)

	add_executable(test-race-camera
		tests/unit/test_race_camera.c
		src/client/race/camera.c)
	target_link_libraries(test-race-camera PRIVATE mh_sim mh_race_scene)
	mh_c89(test-race-camera)
	add_test(NAME race-camera-look-back COMMAND test-race-camera)

	add_executable(test-feedback tests/unit/test_feedback.c)
	target_link_libraries(test-feedback PRIVATE mh_sim)
	mh_c89(test-feedback)
	add_test(NAME feedback-requests COMMAND test-feedback)

	add_executable(test-race-feedback
		tests/unit/test_race_feedback.c
		src/client/race/feedback.c)
	target_link_libraries(test-race-feedback PRIVATE mh_sim)
	mh_c89(test-race-feedback)
	add_test(NAME race-force-feedback COMMAND test-race-feedback)

	add_executable(test-intro-camera tests/unit/test_intro_camera.c)
	target_link_libraries(test-intro-camera PRIVATE mh_sim)
	mh_c89(test-intro-camera)
	add_test(NAME intro-camera COMMAND test-intro-camera)

	add_executable(test-lod tests/unit/test_lod.c)
	target_link_libraries(test-lod PRIVATE mh_assets)
	mh_c89(test-lod)
	add_test(NAME car-model-detail COMMAND test-lod)

	add_executable(test-cheats tests/unit/test_cheats.c)
	target_link_libraries(test-cheats PRIVATE mh_sim)
	mh_c89(test-cheats)
	add_test(NAME hidden-name-triggers COMMAND test-cheats)

	add_executable(test-wheels tests/unit/test_wheels.c)
	target_link_libraries(test-wheels PRIVATE mh_sim)
	mh_c89(test-wheels)
	add_test(NAME wheel-animation COMMAND test-wheels)

	add_executable(test-net tests/unit/test_net.c)
	target_link_libraries(test-net PRIVATE mh_net)
	mh_c89(test-net)
	add_test(NAME network-codec COMMAND test-net)

	add_executable(test-net-wire tests/unit/test_net_wire.c)
	target_link_libraries(test-net-wire PRIVATE mh_net)
	mh_c89(test-net-wire)
	add_test(NAME network-wire-formats COMMAND test-net-wire)

	add_executable(test-ambient tests/unit/test_ambient.c)
	target_link_libraries(test-ambient PRIVATE mh_audio)
	mh_c89(test-ambient)
	add_test(NAME ambient-audio COMMAND test-ambient)

	add_executable(test-surface tests/unit/test_surface.c)
	target_link_libraries(test-surface PRIVATE mh_audio)
	mh_c89(test-surface)
	add_test(NAME surface-audio COMMAND test-surface)

	add_executable(test-surface-cache tests/unit/test_surface_cache.c)
	target_link_libraries(test-surface-cache PRIVATE mh_audio)
	mh_c89(test-surface-cache)
	add_test(NAME surface-material-cache COMMAND test-surface-cache)

	add_executable(test-resource-assets tests/unit/test_resource_assets.c)
	target_link_libraries(test-resource-assets PRIVATE mh_assets)
	mh_c89(test-resource-assets)
	add_test(NAME resource-metadata COMMAND test-resource-assets)

	add_executable(test-atlas tests/unit/test_atlas.c)
	target_link_libraries(test-atlas PRIVATE
		mh_render_common
		mh_assets
		mh_image_fallback
		mh_image_pdi
		mh_resource_memory)
	mh_c89(test-atlas)
	add_test(NAME texture-atlas COMMAND test-atlas)

	add_executable(test-object-frame tests/unit/test_object_frame.c)
	target_link_libraries(test-object-frame PRIVATE mh_sim)
	mh_c89(test-object-frame)
	add_test(NAME scenery-frame-contacts COMMAND test-object-frame)

	add_executable(test-object-selection tests/unit/test_object_selection.c)
	target_link_libraries(test-object-selection PRIVATE mh_sim)
	mh_c89(test-object-selection)
	add_test(NAME scenery-car-selection COMMAND test-object-selection)

	add_executable(test-frame-time tests/unit/test_frame_time.c)
	target_link_libraries(test-frame-time PRIVATE mh_sim)
	mh_c89(test-frame-time)
	add_test(NAME original-frame-time COMMAND test-frame-time)

	add_executable(test-real-clock tests/unit/test_real_clock.c)
	target_link_libraries(test-real-clock PRIVATE mh_sim)
	mh_c89(test-real-clock)
	add_test(NAME original-real-clock COMMAND test-real-clock)

	add_executable(test-process-clock tests/unit/test_process_clock.c)
	target_link_libraries(test-process-clock PRIVATE mh_sim)
	mh_c89(test-process-clock)
	add_test(NAME original-process-clock COMMAND test-process-clock)

	add_executable(test-activity tests/unit/test_activity.c)
	target_link_libraries(test-activity PRIVATE mh_activity)
	mh_c89(test-activity)
	add_test(NAME application-activity COMMAND test-activity)

	add_executable(test-platform-clock tests/unit/test_platform_clock.c)
	target_link_libraries(test-platform-clock PRIVATE mh_game_timer)
	mh_c89(test-platform-clock)
	add_test(NAME portable-platform-clock COMMAND test-platform-clock)

	add_executable(test-clock-source tests/unit/test_clock_source.c)
	target_link_libraries(test-clock-source PRIVATE
		mh_clock_source
		mh_clock)
	mh_c89(test-clock-source)
	add_test(NAME clock-source COMMAND test-clock-source)

	if(TARGET mh_clock_system)
		add_executable(test-system-clock tests/unit/test_system_clock.c)
		target_link_libraries(test-system-clock PRIVATE mh_clock_system)
		mh_c89(test-system-clock)
		add_test(NAME system-clock COMMAND test-system-clock)

		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			add_executable(test-system-clock-platform
				tests/unit/test_system_clock.c)
			target_compile_definitions(test-system-clock-platform PRIVATE
				MH_TEST_CLOCK_WRAP=1)
			target_link_libraries(test-system-clock-platform PRIVATE
				mh_clock_system)
			target_link_options(test-system-clock-platform PRIVATE
				"-Wl,--wrap=clock_gettime"
				"-Wl,--wrap=nanosleep")
			mh_c89(test-system-clock-platform)
			add_test(NAME system-clock-platform COMMAND
				test-system-clock-platform)
		endif()
	endif()

	add_executable(test-object tests/unit/test_object.c)
	target_link_libraries(test-object PRIVATE mh_sim)
	mh_c89(test-object)
	add_test(NAME scenery-object-physics COMMAND test-object)

	add_executable(test-crash tests/unit/test_crash.c)
	target_link_libraries(test-crash PRIVATE mh_audio)
	mh_c89(test-crash)
	add_test(NAME crash-audio COMMAND test-crash)

	add_executable(test-body-audio tests/unit/test_body_audio.c)
	target_link_libraries(test-body-audio PRIVATE mh_audio)
	mh_c89(test-body-audio)
	add_test(NAME body-audio COMMAND test-body-audio)

	add_executable(test-audio-banks tests/unit/test_audio_banks.c)
	target_link_libraries(test-audio-banks PRIVATE
		mh_horn_fallback
		mh_material_mat_srf
		mh_environment_esd
		mh_animation_lightwave
		mh_audio_assets
		mh_resource_memory)
	mh_c89(test-audio-banks)
	add_test(NAME audio-bank-decoders COMMAND test-audio-banks)

	add_executable(test-audio-decoder tests/unit/test_audio_decoder.c)
	target_link_libraries(test-audio-decoder PRIVATE
		mh_audio_fallback
		mh_audio_assets
		mh_resource_memory)
	mh_c89(test-audio-decoder)
	add_test(NAME audio-decoder-providers COMMAND test-audio-decoder)

	add_executable(test-audio-pcm tests/unit/test_audio_pcm.c)
	target_link_libraries(test-audio-pcm PRIVATE mh_audio_pcm)
	mh_c89(test-audio-pcm)
	add_test(NAME audio-pcm-without-container COMMAND test-audio-pcm)

	add_executable(test-audio tests/unit/test_audio.c)
	target_link_libraries(test-audio PRIVATE mh_audio)
	mh_c89(test-audio)
	add_test(NAME native-audio COMMAND test-audio)

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-ds3d-listener
			tests/unit/test_deferred_listener.c)
		target_link_libraries(test-ds3d-listener PRIVATE mh_audio)
		mh_c89(test-ds3d-listener)
		add_test(NAME ds3d-listener COMMAND test-ds3d-listener)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-listener
			tests/unit/test_deferred_listener.c)
		target_compile_definitions(test-a3d12-listener PRIVATE
			MH_TEST_A3D12=1)
		target_link_libraries(test-a3d12-listener PRIVATE mh_audio)
		mh_c89(test-a3d12-listener)
		add_test(NAME a3d12-listener COMMAND test-a3d12-listener)
	endif()

	add_executable(test-spatial-scene tests/unit/test_spatial_scene.c)
	target_link_libraries(test-spatial-scene PRIVATE mh_audio)
	mh_c89(test-spatial-scene)
	add_test(NAME spatial-scene COMMAND test-spatial-scene)

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-ds3d-queue tests/unit/test_ds3d_queue.c)
		target_link_libraries(test-ds3d-queue PRIVATE mh_audio)
		mh_c89(test-ds3d-queue)
		add_test(NAME ds3d-queue COMMAND test-ds3d-queue)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-pcm tests/unit/test_eax_pcm.c)
		target_link_libraries(test-eax-pcm PRIVATE mh_audio)
		mh_c89(test-eax-pcm)
		add_test(NAME eax-pcm COMMAND test-eax-pcm)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-reverb tests/unit/test_reverb.c)
		target_link_libraries(test-reverb PRIVATE mh_audio)
		mh_c89(test-reverb)
		add_test(NAME native-reverb COMMAND test-reverb)
	endif()

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-ds3d-pcm tests/unit/test_bound_pcm.c)
		target_link_libraries(test-ds3d-pcm PRIVATE mh_audio)
		mh_c89(test-ds3d-pcm)
		add_test(NAME ds3d-pcm COMMAND test-ds3d-pcm)
	endif()

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-spatial-effects
			tests/unit/test_bound_effects.c)
		target_link_libraries(test-spatial-effects PRIVATE mh_audio)
		mh_c89(test-spatial-effects)
		add_test(NAME spatial-effects COMMAND test-spatial-effects)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-pcm tests/unit/test_bound_pcm.c)
		target_compile_definitions(test-a3d12-pcm PRIVATE
			MH_TEST_A3D12=1)
		target_link_libraries(test-a3d12-pcm PRIVATE mh_audio)
		mh_c89(test-a3d12-pcm)
		add_test(NAME a3d12-pcm COMMAND test-a3d12-pcm)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-effects
			tests/unit/test_a3d20_effects.c)
		target_link_libraries(test-a3d20-effects PRIVATE mh_audio)
		mh_c89(test-a3d20-effects)
		add_test(NAME a3d20-effects COMMAND test-a3d20-effects)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-acoustics
			tests/unit/test_a3d20_acoustics.c)
		target_link_libraries(test-a3d20-acoustics PRIVATE mh_audio)
		mh_c89(test-a3d20-acoustics)
		add_test(NAME a3d20-acoustics COMMAND test-a3d20-acoustics)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-pcm tests/unit/test_a3d20_pcm.c)
		target_link_libraries(test-a3d20-pcm PRIVATE mh_audio)
		mh_c89(test-a3d20-pcm)
		add_test(NAME a3d20-pcm COMMAND test-a3d20-pcm)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-effects
			tests/unit/test_bound_effects.c)
		target_compile_definitions(test-a3d12-effects PRIVATE
			MH_TEST_A3D12=1)
		target_link_libraries(test-a3d12-effects PRIVATE mh_audio)
		mh_c89(test-a3d12-effects)
		add_test(NAME a3d12-effects COMMAND test-a3d12-effects)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-effects tests/unit/test_eax_effects.c)
		target_link_libraries(test-eax-effects PRIVATE mh_audio)
		mh_c89(test-eax-effects)
		add_test(NAME eax-effects COMMAND test-eax-effects)
	endif()

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-ds3d-source tests/unit/test_bound_source.c)
		target_link_libraries(test-ds3d-source PRIVATE mh_audio)
		mh_c89(test-ds3d-source)
		add_test(NAME ds3d-source COMMAND test-ds3d-source)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-source tests/unit/test_bound_source.c)
		target_compile_definitions(test-a3d12-source PRIVATE
			MH_TEST_A3D12=1)
		target_link_libraries(test-a3d12-source PRIVATE mh_audio)
		mh_c89(test-a3d12-source)
		add_test(NAME a3d12-source COMMAND test-a3d12-source)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-queue tests/unit/test_a3d20_queue.c)
		target_link_libraries(test-a3d20-queue PRIVATE mh_audio)
		mh_c89(test-a3d20-queue)
		add_test(NAME a3d20-queue COMMAND test-a3d20-queue)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-playback
			tests/unit/test_a3d20_playback.c)
		target_link_libraries(test-a3d20-playback PRIVATE mh_audio)
		mh_c89(test-a3d20-playback)
		add_test(NAME a3d20-playback COMMAND test-a3d20-playback)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-source tests/unit/test_a3d20_source.c)
		target_link_libraries(test-a3d20-source PRIVATE mh_audio)
		mh_c89(test-a3d20-source)
		add_test(NAME a3d20-source COMMAND test-a3d20-source)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-listener
			tests/unit/test_a3d20_listener.c)
		target_link_libraries(test-a3d20-listener PRIVATE mh_audio)
		mh_c89(test-a3d20-listener)
		add_test(NAME a3d20-listener COMMAND test-a3d20-listener)
	endif()

	if("ds3d" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-ds3d-playback
			tests/unit/test_panned_playback.c)
		target_link_libraries(test-ds3d-playback PRIVATE mh_audio)
		mh_c89(test-ds3d-playback)
		add_test(NAME ds3d-playback COMMAND test-ds3d-playback)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-playback
			tests/unit/test_panned_playback.c)
		target_compile_definitions(test-a3d12-playback PRIVATE
			MH_TEST_A3D12=1)
		target_link_libraries(test-a3d12-playback PRIVATE mh_audio)
		mh_c89(test-a3d12-playback)
		add_test(NAME a3d12-playback COMMAND test-a3d12-playback)
	endif()

	if("a3d12" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d12-queue tests/unit/test_a3d12_queue.c)
		target_link_libraries(test-a3d12-queue PRIVATE mh_audio)
		mh_c89(test-a3d12-queue)
		add_test(NAME a3d12-queue COMMAND test-a3d12-queue)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-listener tests/unit/test_eax_listener.c)
		target_link_libraries(test-eax-listener PRIVATE mh_audio)
		mh_c89(test-eax-listener)
		add_test(NAME eax-listener COMMAND test-eax-listener)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-source tests/unit/test_eax_source.c)
		target_link_libraries(test-eax-source PRIVATE mh_audio)
		mh_c89(test-eax-source)
		add_test(NAME eax-source COMMAND test-eax-source)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-playback tests/unit/test_eax_playback.c)
		target_link_libraries(test-eax-playback PRIVATE mh_audio)
		mh_c89(test-eax-playback)
		add_test(NAME eax-playback COMMAND test-eax-playback)
	endif()

	if("eax" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-eax-queue tests/unit/test_eax_queue.c)
		target_link_libraries(test-eax-queue PRIVATE mh_audio)
		mh_c89(test-eax-queue)
		add_test(NAME eax-queue COMMAND test-eax-queue)
	endif()

	add_executable(test-spatial-buffer tests/unit/test_spatial_buffer.c)
	target_link_libraries(test-spatial-buffer PRIVATE mh_audio)
	mh_c89(test-spatial-buffer)
	add_test(NAME spatial-buffer COMMAND test-spatial-buffer)

	add_executable(test-audio-catalog tests/unit/test_audio_catalog.c)
	target_link_libraries(test-audio-catalog PRIVATE mh_audio)
	mh_c89(test-audio-catalog)
	add_test(NAME audio-catalog COMMAND test-audio-catalog)

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-geometry
			tests/unit/test_a3d20_geometry.c)
		target_link_libraries(test-a3d20-geometry PRIVATE mh_audio)
		mh_c89(test-a3d20-geometry)
		add_test(NAME a3d20-geometry COMMAND test-a3d20-geometry)
	endif()

	if("a3d20" IN_LIST MH_AUDIO_PROFILES)
		add_executable(test-a3d20-sample tests/unit/test_a3d20_sample.c)
		target_link_libraries(test-a3d20-sample PRIVATE mh_audio)
		mh_c89(test-a3d20-sample)
		add_test(NAME a3d20-sample COMMAND test-a3d20-sample)
	endif()

	add_executable(test-vehicle-sound tests/unit/test_vehicle_sound.c)
	target_link_libraries(test-vehicle-sound PRIVATE mh_audio)
	mh_c89(test-vehicle-sound)
	add_test(NAME vehicle-sound-order COMMAND test-vehicle-sound)

	add_executable(test-sound-pool tests/unit/test_sound_pool.c)
	target_link_libraries(test-sound-pool PRIVATE mh_audio)
	mh_c89(test-sound-pool)
	add_test(NAME sound-channel-pool COMMAND test-sound-pool)

	add_executable(test-effects tests/unit/test_effects.c)
	target_link_libraries(test-effects PRIVATE mh_audio)
	mh_c89(test-effects)
	add_test(NAME shared-effects-playback COMMAND test-effects)

	add_executable(test-effect-pcm tests/unit/test_effect_pcm.c)
	target_link_libraries(test-effect-pcm PRIVATE mh_audio)
	mh_c89(test-effect-pcm)
	add_test(NAME effects-integer-pcm COMMAND test-effect-pcm)

	add_executable(test-effect-buffer tests/unit/test_effect_buffer.c)
	target_link_libraries(test-effect-buffer PRIVATE mh_audio)
	mh_c89(test-effect-buffer)
	add_test(NAME effects-buffer COMMAND test-effect-buffer)

	add_executable(test-effect-output tests/unit/test_effect_output.c)
	target_link_libraries(test-effect-output PRIVATE mh_audio)
	mh_c89(test-effect-output)
	add_test(NAME effects-ring-output COMMAND test-effect-output)

	add_executable(test-menu-sound tests/unit/test_menu_sound.c)
	target_link_libraries(test-menu-sound PRIVATE mh_audio)
	mh_c89(test-menu-sound)
	add_test(NAME pause-menu-sound COMMAND test-menu-sound)

	add_executable(test-frontend-sound tests/unit/test_frontend_sound.c)
	target_link_libraries(test-frontend-sound PRIVATE mh_audio)
	mh_c89(test-frontend-sound)
	add_test(NAME frontend-sound-playback COMMAND test-frontend-sound)

	add_executable(test-cd-selection tests/integration/cd_selection.c)
	target_link_libraries(test-cd-selection PRIVATE mh_audio)
	mh_c89(test-cd-selection)
	add_test(NAME cd-selection COMMAND test-cd-selection)

	add_executable(test-engine-burst tests/unit/test_engine_burst.c)
	target_link_libraries(test-engine-burst PRIVATE mh_audio)
	mh_c89(test-engine-burst)
	add_test(NAME engine-braking-bursts COMMAND test-engine-burst)

	add_executable(test-horn tests/unit/test_horn.c)
	target_link_libraries(test-horn PRIVATE mh_audio)
	mh_c89(test-horn)
	add_test(NAME car-horns COMMAND test-horn)

	add_executable(test-track-flag tests/unit/test_track_flag.c)
	target_link_libraries(test-track-flag PRIVATE mh_assets)
	mh_c89(test-track-flag)
	add_test(NAME track-unlock-flags COMMAND test-track-flag)

	add_executable(test-league-unlocks tests/unit/test_league_unlocks.c)
	target_link_libraries(test-league-unlocks PRIVATE mh_sim)
	mh_c89(test-league-unlocks)
	add_test(NAME league-unlock-awards COMMAND test-league-unlocks)

	add_executable(test-results tests/unit/test_results.c)
	target_link_libraries(test-results PRIVATE mh_ui)
	mh_c89(test-results)
	add_test(NAME race-results COMMAND test-results)

	add_executable(test-font-atlas tests/unit/test_font_atlas.c)
	target_link_libraries(test-font-atlas PRIVATE mh_ui)
	mh_c89(test-font-atlas)
	add_test(NAME font-atlas COMMAND test-font-atlas)

	add_executable(test-ui-assets tests/unit/test_ui_assets.c)
	target_link_libraries(test-ui-assets PRIVATE mh_assets)
	mh_c89(test-ui-assets)
	add_test(NAME ui-asset-readers COMMAND test-ui-assets)

	add_executable(test-spr tests/unit/test_spr.c)
	target_link_libraries(test-spr PRIVATE
		mh_assets
		mh_sprite_spr
		mh_resource_memory)
	mh_c89(test-spr)
	add_test(NAME sprite-archive COMMAND test-spr)

	add_executable(test-hud tests/unit/test_hud.c)
	target_link_libraries(test-hud PRIVATE mh_ui)
	mh_c89(test-hud)
	add_test(NAME hud-layout COMMAND test-hud)

	add_executable(test-software-hud tests/unit/test_software_hud.c)
	target_link_libraries(test-software-hud PRIVATE mh_ui)
	mh_c89(test-software-hud)
	add_test(NAME software-hud-layout COMMAND test-software-hud)

	add_executable(test-software-hud-map tests/unit/test_software_hud_map.c)
	target_link_libraries(test-software-hud-map PRIVATE mh_ui)
	mh_c89(test-software-hud-map)
	add_test(NAME software-hud-map COMMAND test-software-hud-map)

	add_executable(test-software-hud-text
		tests/unit/test_software_hud_text.c)
	target_link_libraries(test-software-hud-text PRIVATE mh_ui)
	mh_c89(test-software-hud-text)
	add_test(NAME software-hud-text COMMAND test-software-hud-text)

	add_executable(test-race-info tests/unit/test_race_info.c)
	target_link_libraries(test-race-info PRIVATE mh_ui)
	mh_c89(test-race-info)
	add_test(NAME race-info-overlay COMMAND test-race-info)

	add_executable(test-race-startup
		tests/unit/test_race_startup.c
		src/client/race/startup.c)
	target_link_libraries(test-race-startup PRIVATE mh_core)
	mh_c89(test-race-startup)
	add_test(NAME race-loading-startup COMMAND test-race-startup)

	add_executable(test-loading-confirmation
		tests/unit/test_loading_confirmation.c)
	target_link_libraries(test-loading-confirmation PRIVATE mh_application)
	mh_c89(test-loading-confirmation)
	add_test(NAME loading-confirmation COMMAND test-loading-confirmation)

	add_executable(test-loading-screen tests/unit/test_loading_screen.c)
	target_link_libraries(test-loading-screen PRIVATE mh_ui)
	mh_c89(test-loading-screen)
	add_test(NAME loading-screen COMMAND test-loading-screen)

	add_executable(test-messages tests/unit/test_messages.c)
	target_link_libraries(test-messages PRIVATE mh_ui)
	mh_c89(test-messages)
	add_test(NAME message-history COMMAND test-messages)

	add_executable(test-playback-hud tests/unit/test_playback_hud.c)
	target_link_libraries(test-playback-hud PRIVATE mh_playback_hud)
	mh_c89(test-playback-hud)
	add_test(NAME playback-messages COMMAND test-playback-hud)

	add_executable(test-pause tests/unit/test_pause.c)
	target_link_libraries(test-pause PRIVATE mh_ui)
	mh_c89(test-pause)
	add_test(NAME pause-menu-controller COMMAND test-pause)

	add_executable(test-menu tests/unit/test_menu.c)
	target_link_libraries(test-menu PRIVATE mh_ui)
	mh_c89(test-menu)
	add_test(NAME frontend-menu-controller COMMAND test-menu)

	add_executable(test-attract tests/unit/test_attract.c)
	target_link_libraries(test-attract PRIVATE mh_ui)
	mh_c89(test-attract)
	add_test(NAME attract-policy COMMAND test-attract)

	add_executable(test-menu-sprite tests/unit/test_menu_sprite.c)
	target_link_libraries(test-menu-sprite PRIVATE mh_ui)
	mh_c89(test-menu-sprite)
	add_test(NAME frontend-sprite-effects COMMAND test-menu-sprite)

	add_executable(test-menu-needle tests/unit/test_menu_needle.c)
	target_link_libraries(test-menu-needle PRIVATE mh_ui)
	mh_c89(test-menu-needle)
	add_test(NAME frontend-needle COMMAND test-menu-needle)

	add_executable(test-menu-background tests/unit/test_menu_background.c)
	target_link_libraries(test-menu-background PRIVATE mh_ui)
	mh_c89(test-menu-background)
	add_test(NAME frontend-background COMMAND test-menu-background)

	add_executable(test-lob tests/unit/test_lob.c)
	target_link_libraries(test-lob PRIVATE mh_assets)
	mh_c89(test-lob)
	add_test(NAME line-object-assets COMMAND test-lob)

	add_executable(test-menu-wire tests/unit/test_menu_wire.c)
	target_link_libraries(test-menu-wire PRIVATE mh_ui)
	mh_c89(test-menu-wire)
	add_test(NAME frontend-wireframes COMMAND test-menu-wire)

	add_executable(test-thumbnail-image tests/unit/test_thumbnail_image.c)
	target_link_libraries(test-thumbnail-image PRIVATE
		mh_resource_memory
		mh_ui
		mh_image_tga)
	mh_c89(test-thumbnail-image)
	add_test(NAME thumbnail-image-provider COMMAND test-thumbnail-image)

	add_executable(test-backdrop-image tests/unit/test_backdrop_image.c)
	target_link_libraries(test-backdrop-image PRIVATE
		mh_resource_memory
		mh_ui
		mh_lobby_ui)
	mh_c89(test-backdrop-image)
	add_test(NAME backdrop-image-provider COMMAND test-backdrop-image)

	add_executable(test-menu-backdrop tests/unit/test_menu_backdrop.c)
	target_link_libraries(test-menu-backdrop PRIVATE
		mh_file_read_filesystem
		mh_ui)
	mh_c89(test-menu-backdrop)
	add_test(NAME frontend-backdrop COMMAND test-menu-backdrop)

	add_executable(test-menu-configuration
		tests/unit/test_menu_configuration.c)
	target_link_libraries(test-menu-configuration PRIVATE mh_menus_app)
	mh_c89(test-menu-configuration)
	add_test(NAME menu-feature-configuration COMMAND
		test-menu-configuration)

	add_executable(test-preferences tests/unit/test_preferences.c)
	target_link_libraries(test-preferences PRIVATE
		mh_menus_app
		mh_resource_memory)
	mh_c89(test-preferences)
	add_test(NAME public-preferences COMMAND test-preferences)

	add_executable(test-race-settings tests/unit/test_race_settings.c)
	target_link_libraries(test-race-settings PRIVATE mh_menus_app)
	mh_c89(test-race-settings)
	add_test(NAME race-frontend-settings COMMAND test-race-settings)

	add_executable(test-menu-main tests/unit/test_menu_main.c)
	target_link_libraries(test-menu-main PRIVATE mh_ui mh_sprite_spr)
	mh_c89(test-menu-main)
	add_test(NAME frontend-main-composition COMMAND test-menu-main)

	add_executable(test-menu-dialog tests/unit/test_menu_dialog.c)
	target_link_libraries(test-menu-dialog PRIVATE mh_ui)
	mh_c89(test-menu-dialog)
	add_test(NAME frontend-dialog-composition COMMAND test-menu-dialog)

	add_executable(test-menu-single tests/unit/test_menu_single.c)
	target_link_libraries(test-menu-single PRIVATE mh_ui)
	mh_c89(test-menu-single)
	add_test(NAME frontend-single-player COMMAND test-menu-single)

	add_executable(test-menu-race-setup tests/unit/test_menu_race_setup.c)
	target_link_libraries(test-menu-race-setup PRIVATE mh_ui)
	mh_c89(test-menu-race-setup)
	add_test(NAME frontend-race-setup COMMAND test-menu-race-setup)

	add_executable(test-menu-car-setup tests/unit/test_menu_car_setup.c)
	target_link_libraries(test-menu-car-setup PRIVATE mh_ui)
	mh_c89(test-menu-car-setup)
	add_test(NAME frontend-car-setup COMMAND test-menu-car-setup)

	if(MH_ALL_AUDIO_PROFILES)
		add_executable(test-menu-sound-options
			tests/unit/test_menu_sound_options.c)
		target_link_libraries(test-menu-sound-options PRIVATE mh_ui)
		mh_c89(test-menu-sound-options)
		add_test(NAME frontend-sound-options COMMAND
			test-menu-sound-options)
	endif()

	add_executable(test-menu-sound-draw tests/unit/test_menu_sound_draw.c)
	target_link_libraries(test-menu-sound-draw PRIVATE mh_ui)
	mh_c89(test-menu-sound-draw)
	add_test(NAME frontend-sound-compositor COMMAND test-menu-sound-draw)

	add_executable(test-menu-options tests/unit/test_menu_options.c)
	target_link_libraries(test-menu-options PRIVATE mh_ui mh_sprite_spr)
	mh_c89(test-menu-options)
	add_test(NAME frontend-options COMMAND test-menu-options)

	add_executable(test-menu-controls tests/unit/test_menu_controls.c)
	target_link_libraries(test-menu-controls PRIVATE mh_ui)
	mh_c89(test-menu-controls)
	add_test(NAME frontend-controls COMMAND test-menu-controls)

	add_executable(test-menu-controls-draw
		tests/unit/test_menu_controls_draw.c)
	target_link_libraries(test-menu-controls-draw PRIVATE
		mh_ui
		mh_sprite_spr)
	mh_c89(test-menu-controls-draw)
	add_test(NAME frontend-controls-composition COMMAND
		test-menu-controls-draw)

	add_executable(test-menu-graphics-fps
		tests/unit/test_menu_graphics_fps.c)
	target_link_libraries(test-menu-graphics-fps PRIVATE mh_ui mh_settings)
	mh_c89(test-menu-graphics-fps)
	add_test(NAME frontend-graphics-fps-input COMMAND
		test-menu-graphics-fps)

	if(MH_ALL_SCREENSHOT_FORMATS)
		add_executable(test-menu-graphics
			tests/unit/test_menu_graphics.c)
		target_link_libraries(test-menu-graphics PRIVATE
			mh_ui
			mh_settings)
		mh_c89(test-menu-graphics)
		add_test(NAME frontend-graphics COMMAND test-menu-graphics)
	endif()

	add_executable(test-menu-personal tests/unit/test_menu_personal.c)
	target_link_libraries(test-menu-personal PRIVATE mh_ui mh_settings)
	mh_c89(test-menu-personal)
	add_test(NAME frontend-personal COMMAND test-menu-personal)

	add_executable(test-menu-multi tests/unit/test_menu_multi.c)
	target_link_libraries(test-menu-multi PRIVATE mh_ui mh_settings)
	mh_c89(test-menu-multi)
	add_test(NAME frontend-multiplayer-input COMMAND test-menu-multi)

	add_executable(test-menu-multi-draw tests/unit/test_menu_multi_draw.c)
	target_link_libraries(test-menu-multi-draw PRIVATE
		mh_ui
		mh_settings
		mh_sprite_spr)
	mh_c89(test-menu-multi-draw)
	add_test(NAME frontend-multiplayer-composition COMMAND
		test-menu-multi-draw)

	add_executable(test-menu-personal-draw
		tests/unit/test_menu_personal_draw.c)
	target_link_libraries(test-menu-personal-draw PRIVATE
		mh_ui
		mh_settings
		mh_sprite_spr)
	mh_c89(test-menu-personal-draw)
	add_test(NAME frontend-personal-composition COMMAND
		test-menu-personal-draw)

	add_executable(test-menu-credits tests/unit/test_menu_credits.c)
	target_link_libraries(test-menu-credits PRIVATE mh_ui mh_sprite_spr)
	mh_c89(test-menu-credits)
	add_test(NAME frontend-credits COMMAND test-menu-credits)

	add_executable(test-menu-rankings tests/unit/test_menu_rankings.c)
	target_link_libraries(test-menu-rankings PRIVATE mh_ui)
	mh_c89(test-menu-rankings)
	add_test(NAME frontend-rankings COMMAND test-menu-rankings)

	add_executable(test-menu-rankings-draw
		tests/unit/test_menu_rankings_draw.c)
	target_link_libraries(test-menu-rankings-draw PRIVATE
		mh_ui
		mh_sprite_spr)
	mh_c89(test-menu-rankings-draw)
	add_test(NAME frontend-rankings-draw COMMAND test-menu-rankings-draw)

	add_executable(test-menu-graphics-draw
		tests/unit/test_menu_graphics_draw.c)
	target_link_libraries(test-menu-graphics-draw PRIVATE
		mh_ui
		mh_settings
		mh_sprite_spr)
	mh_c89(test-menu-graphics-draw)
	add_test(NAME frontend-graphics-composition COMMAND
		test-menu-graphics-draw)

	add_executable(test-menu-ghost tests/unit/test_menu_ghost.c)
	target_link_libraries(test-menu-ghost PRIVATE mh_ui)
	mh_c89(test-menu-ghost)
	add_test(NAME frontend-ghost-controller COMMAND test-menu-ghost)

	add_executable(test-menu-ghost-draw tests/unit/test_menu_ghost_draw.c)
	target_link_libraries(test-menu-ghost-draw PRIVATE mh_ui)
	mh_c89(test-menu-ghost-draw)
	add_test(NAME frontend-ghost-compositor COMMAND test-menu-ghost-draw)

	add_executable(test-menu-demo-catalog
		tests/unit/test_menu_demo_catalog.c)
	target_link_libraries(test-menu-demo-catalog PRIVATE mh_ui)
	mh_c89(test-menu-demo-catalog)
	add_test(NAME frontend-demo-catalog COMMAND test-menu-demo-catalog)

	add_executable(test-menu-gauge tests/unit/test_menu_gauge.c)
	target_link_libraries(test-menu-gauge PRIVATE mh_ui)
	mh_c89(test-menu-gauge)
	add_test(NAME frontend-car-gauges COMMAND test-menu-gauge)

	add_executable(test-menu-widgets tests/unit/test_menu_widgets.c)
	target_link_libraries(test-menu-widgets PRIVATE
		mh_file_read_filesystem
		mh_ui)
	mh_c89(test-menu-widgets)
	add_test(NAME frontend-preview-widgets COMMAND test-menu-widgets)

	add_executable(test-menu-car-draw tests/unit/test_menu_car_draw.c)
	target_link_libraries(test-menu-car-draw PRIVATE mh_ui)
	mh_c89(test-menu-car-draw)
	add_test(NAME frontend-car-compositor COMMAND test-menu-car-draw)

	add_executable(test-portrait tests/unit/test_portrait.c)
	target_link_libraries(test-portrait PRIVATE
		mh_file_read_filesystem
		mh_assets)
	mh_c89(test-portrait)
	add_test(NAME results-portraits COMMAND test-portrait)

	add_executable(test-menu-catalog tests/unit/test_menu_catalog.c)
	target_link_libraries(test-menu-catalog PRIVATE
		mh_file_read_filesystem
		mh_ui)
	mh_c89(test-menu-catalog)
	add_test(NAME frontend-catalog-order COMMAND test-menu-catalog)

	add_executable(test-menu-unlocks tests/unit/test_menu_unlocks.c)
	target_link_libraries(test-menu-unlocks PRIVATE mh_ui)
	mh_c89(test-menu-unlocks)
	add_test(NAME frontend-catalog-unlocks COMMAND test-menu-unlocks)

	add_executable(test-keyboard tests/unit/test_keyboard.c)
	target_link_libraries(test-keyboard PRIVATE mh_input)
	mh_c89(test-keyboard)
	add_test(NAME keyboard-repeat COMMAND test-keyboard)

	add_executable(test-binding tests/unit/test_binding.c)
	target_link_libraries(test-binding PRIVATE mh_input mh_assets)
	mh_c89(test-binding)
	add_test(NAME control-binding-query COMMAND test-binding)

	add_executable(test-drive-controls tests/unit/test_drive_controls.c)
	target_link_libraries(test-drive-controls PRIVATE mh_input mh_assets)
	mh_c89(test-drive-controls)
	add_test(NAME control-driving-shaping COMMAND test-drive-controls)

	add_executable(test-view-controls tests/unit/test_view_controls.c)
	target_link_libraries(test-view-controls PRIVATE mh_input mh_assets)
	mh_c89(test-view-controls)
	add_test(NAME control-camera-requests COMMAND test-view-controls)

	add_executable(test-screenshot tests/unit/test_screenshot.c)
	target_link_libraries(test-screenshot PRIVATE mh_input mh_assets)
	mh_c89(test-screenshot)
	add_test(NAME screenshot-control-and-picture COMMAND test-screenshot)

	add_executable(test-devices tests/unit/test_devices.c)
	target_link_libraries(test-devices PRIVATE mh_input)
	mh_c89(test-devices)
	add_test(NAME input-device-synthesis COMMAND test-devices)

	add_executable(test-text-edit tests/unit/test_text_edit.c)
	target_link_libraries(test-text-edit PRIVATE mh_input mh_assets)
	mh_c89(test-text-edit)
	add_test(NAME byte-text-editor COMMAND test-text-edit)

	add_executable(test-chat tests/unit/test_chat.c)
	target_link_libraries(test-chat PRIVATE mh_input mh_assets)
	mh_c89(test-chat)
	add_test(NAME race-chat-input COMMAND test-chat)

	add_executable(test-chat-draw tests/unit/test_chat_draw.c)
	target_link_libraries(test-chat-draw PRIVATE mh_ui)
	mh_c89(test-chat-draw)
	add_test(NAME race-chat-overlay COMMAND test-chat-draw)

	add_executable(test-lobby-console tests/unit/test_lobby_console.c)
	target_link_libraries(test-lobby-console PRIVATE
		mh_file_read_filesystem
		mh_ui
		mh_input)
	mh_c89(test-lobby-console)
	add_test(NAME network-lobby-console COMMAND test-lobby-console)

	add_executable(test-lobby-screen tests/unit/test_lobby_screen.c)
	target_link_libraries(test-lobby-screen PRIVATE mh_lobby_ui)
	mh_c89(test-lobby-screen)
	mh_protocol_test("legacy" network-lobby-screen test-lobby-screen)

	add_executable(test-lobby-settings tests/unit/test_lobby_settings.c)
	target_link_libraries(test-lobby-settings PRIVATE mh_lobby_ui)
	mh_c89(test-lobby-settings)
	add_test(NAME network-lobby-settings COMMAND test-lobby-settings)

	add_executable(test-lobby-roster tests/unit/test_lobby_roster.c)
	target_link_libraries(test-lobby-roster PRIVATE
		mh_file_read_filesystem
		mh_ui)
	mh_c89(test-lobby-roster)
	add_test(NAME network-lobby-roster COMMAND test-lobby-roster)

	add_executable(test-lobby-keys tests/unit/test_lobby_keys.c)
	target_link_libraries(test-lobby-keys PRIVATE mh_input mh_sim)
	mh_c89(test-lobby-keys)
	add_test(NAME network-lobby-keys COMMAND test-lobby-keys)

	add_executable(test-lobby-commands tests/unit/test_lobby_commands.c)
	target_link_libraries(test-lobby-commands PRIVATE mh_net)
	mh_c89(test-lobby-commands)
	add_test(NAME network-lobby-commands COMMAND test-lobby-commands)

	add_executable(test-menu-recording tests/unit/test_menu_recording.c)
	target_link_libraries(test-menu-recording PRIVATE
		mh_file_read_filesystem
		mh_ui)
	mh_c89(test-menu-recording)
	add_test(NAME recording-save-menu COMMAND test-menu-recording)

	add_executable(test-menu-league tests/unit/test_menu_league.c)
	target_link_libraries(test-menu-league PRIVATE mh_ui)
	mh_c89(test-menu-league)
	add_test(NAME league-menu-controls COMMAND test-menu-league)

	add_executable(test-menu-league-draw tests/unit/test_menu_league_draw.c)
	target_link_libraries(test-menu-league-draw PRIVATE mh_ui)
	mh_c89(test-menu-league-draw)
	add_test(NAME league-menu-drawing COMMAND test-menu-league-draw)

	add_executable(test-settings tests/unit/test_settings.c)
	target_link_libraries(test-settings PRIVATE mh_settings)
	mh_c89(test-settings)
	add_test(NAME settings-parser COMMAND test-settings)

	add_executable(test-frontend-settings
		tests/unit/test_frontend_settings.c)
	target_link_libraries(test-frontend-settings PRIVATE mh_settings)
	mh_c89(test-frontend-settings)
	add_test(NAME frontend-settings-return COMMAND test-frontend-settings)

	add_executable(test-config-assets tests/unit/test_config_assets.c)
	target_link_libraries(test-config-assets PRIVATE mh_settings mh_ui)
	mh_c89(test-config-assets)
	add_test(NAME config-assets COMMAND test-config-assets)

	add_executable(test-control-write tests/unit/test_control_write.c)
	target_link_libraries(test-control-write PRIVATE mh_settings mh_assets)
	mh_c89(test-control-write)
	add_test(NAME control-layout-writer COMMAND test-control-write)

	add_executable(test-control-catalog tests/unit/test_control_catalog.c)
	target_link_libraries(test-control-catalog PRIVATE
		mh_settings
		mh_assets)
	mh_c89(test-control-catalog)
	add_test(NAME control-profile-catalog COMMAND test-control-catalog)

	add_executable(test-address-book tests/unit/test_address_book.c)
	target_link_libraries(test-address-book PRIVATE mh_settings mh_assets)
	mh_c89(test-address-book)
	add_test(NAME tcp-address-book COMMAND test-address-book)

	add_executable(test-tron-intro tests/unit/test_tron_intro.c)
	target_link_libraries(test-tron-intro PRIVATE mh_ui)
	mh_c89(test-tron-intro)
	add_test(NAME tron-introduction COMMAND test-tron-intro)

	add_executable(test-standings tests/unit/test_standings.c)
	target_link_libraries(test-standings PRIVATE mh_sim)
	mh_c89(test-standings)
	add_test(NAME race-standings COMMAND test-standings)

	add_executable(test-league-race tests/unit/test_league_race.c)
	target_link_libraries(test-league-race PRIVATE mh_sim)
	mh_c89(test-league-race)
	add_test(NAME league-race COMMAND test-league-race)

	add_executable(test-league-catalog tests/unit/test_league_catalog.c)
	target_link_libraries(test-league-catalog PRIVATE mh_sim)
	mh_c89(test-league-catalog)
	add_test(NAME league-catalog COMMAND test-league-catalog)

	add_executable(test-quick-race tests/unit/test_quick_race.c)
	target_link_libraries(test-quick-race PRIVATE mh_sim)
	mh_c89(test-quick-race)
	add_test(NAME quick-race COMMAND test-quick-race)

	add_executable(test-pair tests/unit/test_pair.c)
	target_link_libraries(test-pair PRIVATE mh_sim)
	mh_c89(test-pair)
	add_test(NAME car-pair-contact COMMAND test-pair)

	add_executable(test-recovery tests/unit/test_recovery.c)
	target_link_libraries(test-recovery PRIVATE mh_sim)
	mh_c89(test-recovery)
	add_test(NAME automatic-recovery COMMAND test-recovery)

	add_executable(test-ai-traffic tests/unit/test_ai_traffic.c)
	target_link_libraries(test-ai-traffic PRIVATE mh_sim)
	mh_c89(test-ai-traffic)
	add_test(NAME ai-traffic COMMAND test-ai-traffic)

	add_executable(test-opponent tests/unit/test_opponent.c)
	target_link_libraries(test-opponent PRIVATE mh_sim)
	mh_c89(test-opponent)
	add_test(NAME opponent-tuning COMMAND test-opponent)

	add_executable(test-ai-driver tests/unit/test_ai_driver.c)
	target_link_libraries(test-ai-driver PRIVATE
		mh_file_read_filesystem
		mh_sim)
	mh_c89(test-ai-driver)
	add_test(NAME ai-driver COMMAND test-ai-driver)

	add_executable(test-ai-roster tests/unit/test_ai_roster.c)
	target_link_libraries(test-ai-roster PRIVATE
		mh_file_read_filesystem
		mh_sim)
	mh_c89(test-ai-roster)
	add_test(NAME ai-roster COMMAND test-ai-roster)

	add_executable(test-ai tests/unit/test_ai.c)
	target_link_libraries(test-ai PRIVATE mh_sim)
	mh_c89(test-ai)
	add_test(NAME ai-path COMMAND test-ai)

	add_executable(test-highscore tests/unit/test_highscore.c)
	target_link_libraries(test-highscore PRIVATE mh_sim)
	mh_c89(test-highscore)
	add_test(NAME high-score-tables COMMAND test-highscore)

	add_executable(test-highscore-awards tests/unit/test_highscore_awards.c)
	target_link_libraries(test-highscore-awards PRIVATE mh_sim)
	mh_c89(test-highscore-awards)
	add_test(NAME high-score-awards COMMAND test-highscore-awards)

	add_executable(test-records tests/unit/test_records.c)
	target_link_libraries(test-records PRIVATE mh_sim)
	mh_c89(test-records)
	add_test(NAME race-records COMMAND test-records)

	add_executable(test-race tests/unit/test_race.c)
	target_link_libraries(test-race PRIVATE mh_sim)
	mh_c89(test-race)
	add_test(NAME race-progress COMMAND test-race)

	add_executable(test-race-gaps tests/unit/test_race_gaps.c)
	target_link_libraries(test-race-gaps PRIVATE mh_ui)
	mh_c89(test-race-gaps)
	add_test(NAME race-gaps COMMAND test-race-gaps)

	add_executable(test-placement tests/unit/test_placement.c)
	target_link_libraries(test-placement PRIVATE mh_sim)
	mh_c89(test-placement)
	add_test(NAME race-placement COMMAND test-placement)

	add_executable(test-route-curve tests/unit/test_route_curve.c)
	target_link_libraries(test-route-curve PRIVATE
		mh_file_read_filesystem
		mh_sim)
	mh_c89(test-route-curve)
	add_test(NAME route-curve COMMAND test-route-curve)

	add_executable(test-route-projection tests/unit/test_route_projection.c)
	target_link_libraries(test-route-projection PRIVATE
		mh_file_read_filesystem
		mh_sim)
	mh_c89(test-route-projection)
	add_test(NAME route-projection COMMAND test-route-projection)

	add_executable(test-engine tests/unit/test_engine.c)
	target_link_libraries(test-engine PRIVATE mh_sim)
	mh_c89(test-engine)
	add_test(NAME engine-gearbox COMMAND test-engine)

	add_executable(test-grip tests/unit/test_grip.c)
	target_link_libraries(test-grip PRIVATE mh_sim)
	mh_c89(test-grip)
	add_test(NAME surface-grip COMMAND test-grip)

	add_executable(test-suspension-cache tests/unit/test_suspension_cache.c)
	target_link_libraries(test-suspension-cache PRIVATE mh_sim)
	mh_c89(test-suspension-cache)
	add_test(NAME suspension-plane-cache COMMAND test-suspension-cache)

	add_executable(test-suspension-forces
		tests/unit/test_suspension_forces.c)
	target_link_libraries(test-suspension-forces PRIVATE mh_sim)
	mh_c89(test-suspension-forces)
	add_test(NAME suspension-force-stores COMMAND test-suspension-forces)

	add_executable(test-motion-stores tests/unit/test_motion_stores.c)
	target_link_libraries(test-motion-stores PRIVATE mh_sim)
	mh_c89(test-motion-stores)
	add_test(NAME motion-translation-stores COMMAND test-motion-stores)

	add_executable(test-forces tests/unit/test_forces.c)
	target_link_libraries(test-forces PRIVATE mh_sim)
	mh_c89(test-forces)
	add_test(NAME vehicle-forces COMMAND test-forces)

	add_executable(test-collision-source
		tests/unit/test_collision_source.c
		tests/support/collision_source.c)
	target_link_libraries(test-collision-source PRIVATE
		mh_collision_fallback
		mh_collision_loading
		mh_resource_memory
		mh_sim)
	mh_c89(test-collision-source)
	add_test(NAME collision-source COMMAND test-collision-source)

	add_executable(test-collision-data tests/unit/test_collision_data.c)
	target_link_libraries(test-collision-data PRIVATE mh_assets)
	mh_c89(test-collision-data)
	add_test(NAME collision-data COMMAND test-collision-data)

	add_executable(test-collision tests/unit/test_collision.c)
	target_link_libraries(test-collision PRIVATE mh_scene_models)
	mh_c89(test-collision)

	if(MINGW)
		# This fixture retains several whole race states for comparisons.
		# Match the Linux test stack instead of MinGW's 2 MiB default.
		target_link_options(test-collision PRIVATE
			"-Wl,--stack,8388608")
	endif()
	add_test(NAME collision-contact COMMAND test-collision)

	add_executable(test-lighting tests/unit/test_lighting.c)
	target_link_libraries(test-lighting PRIVATE mh_assets)
	mh_c89(test-lighting)
	add_test(NAME world-lighting COMMAND test-lighting)

	add_executable(test-glenz tests/unit/test_glenz.c)
	target_link_libraries(test-glenz PRIVATE mh_render_common)
	mh_c89(test-glenz)
	add_test(NAME glenz-coordinates COMMAND test-glenz)

	add_executable(test-hidden-weather tests/unit/test_hidden_weather.c)
	target_link_libraries(test-hidden-weather PRIVATE mh_atmosphere_mesh)
	mh_c89(test-hidden-weather)
	add_test(NAME hidden-weather COMMAND test-hidden-weather)

	add_executable(test-brightness tests/unit/test_brightness.c)
	target_link_libraries(test-brightness PRIVATE mh_assets)
	mh_c89(test-brightness)
	add_test(NAME display-brightness COMMAND test-brightness)

	add_executable(test-tron tests/unit/test_tron.c)
	target_link_libraries(test-tron PRIVATE mh_assets)
	mh_c89(test-tron)
	add_test(NAME tron-outline COMMAND test-tron)

	add_executable(test-near-clip tests/unit/test_near_clip.c)
	target_link_libraries(test-near-clip PRIVATE mh_assets)
	mh_c89(test-near-clip)
	add_test(NAME near-plane-clipping COMMAND test-near-clip)

	add_executable(test-projection tests/unit/test_projection.c)
	target_link_libraries(test-projection PRIVATE mh_assets)
	mh_c89(test-projection)
	add_test(NAME point-projection COMMAND test-projection)

	add_executable(test-name-label tests/unit/test_name_label.c)
	target_link_libraries(test-name-label PRIVATE
		mh_file_read_filesystem
		mh_assets)
	mh_c89(test-name-label)
	add_test(NAME name-label-projection COMMAND test-name-label)

	add_executable(test-frustum tests/unit/test_frustum.c)
	target_link_libraries(test-frustum PRIVATE mh_assets)
	mh_c89(test-frustum)
	add_test(NAME object-frustum COMMAND test-frustum)

	add_executable(test-world-view tests/unit/test_world_view.c)
	target_link_libraries(test-world-view PRIVATE mh_assets)
	mh_c89(test-world-view)
	add_test(NAME world-view COMMAND test-world-view)

	add_executable(test-scene-face tests/unit/test_scene_face.c)
	target_link_libraries(test-scene-face PRIVATE mh_assets)
	mh_c89(test-scene-face)
	add_test(NAME scene-face-preparation COMMAND test-scene-face)

	add_executable(test-effect-policy tests/unit/test_effect_policy.c)
	target_link_libraries(test-effect-policy PRIVATE mh_render_common)
	mh_c89(test-effect-policy)
	add_test(NAME scene-effect-policy COMMAND test-effect-policy)

	add_executable(test-material-policy
		tests/unit/test_material_policy.c
		src/backends/render/shader_material.c)
	target_link_libraries(test-material-policy PRIVATE mh_scene_materials)
	mh_c89(test-material-policy)
	add_test(NAME material-profile-policy COMMAND test-material-policy)

	add_executable(test-material-face tests/unit/test_material_face.c)
	target_link_libraries(test-material-face PRIVATE mh_scene_materials)
	mh_c89(test-material-face)
	add_test(NAME material-face-preparation COMMAND test-material-face)

	add_executable(mh-demo-sources tests/assets/demo_sources.c)
	target_link_libraries(mh-demo-sources PRIVATE
		mh_catalog_fallback
		mh_horn_fallback
		mh_font_fallback
		mh_image_fallback
		mh_audio_fallback
		mh_resource_memory
		mh_resource_filesystem)
	mh_c89(mh-demo-sources)

	add_executable(test-retro-source
		tests/unit/test_retro_source.c
		tests/support/retro_source.c)
	target_link_libraries(test-retro-source PRIVATE
		mh_retro_loading
		mh_race_scene
		mh_resource_memory)
	mh_c89(test-retro-source)
	add_test(NAME retro-source COMMAND test-retro-source)

	add_executable(test-retro tests/unit/test_retro.c)
	target_link_libraries(test-retro PRIVATE
		mh_file_read_filesystem
		mh_assets)
	mh_c89(test-retro)
	add_test(NAME retro-animation COMMAND test-retro)

	add_executable(test-retro-draw tests/unit/test_retro_draw.c)
	target_link_libraries(test-retro-draw PRIVATE
		mh_retro_fallback
		mh_resource_filesystem
		mh_ui)
	mh_c89(test-retro-draw)
	add_test(NAME retro-draw COMMAND test-retro-draw)

	add_executable(test-unavailable tests/unit/test_unavailable.c)
	target_link_libraries(test-unavailable PRIVATE mh_render_common)
	mh_c89(test-unavailable)
	add_test(NAME unavailable-material COMMAND test-unavailable)

	add_executable(test-wireframe-source
		tests/unit/test_wireframe_source.c
		tests/support/wireframe_source.c)
	target_link_libraries(test-wireframe-source PRIVATE
		mh_assets
		mh_wireframe
		mh_resource_memory)
	mh_c89(test-wireframe-source)
	add_test(NAME wireframe-source COMMAND test-wireframe-source)

	add_executable(test-atlas-source
		tests/unit/test_atlas_source.c
		tests/support/atlas_source.c)
	target_link_libraries(test-atlas-source PRIVATE
		mh_assets
		mh_atlas_map
		mh_resource_memory)
	mh_c89(test-atlas-source)
	add_test(NAME atlas-source COMMAND test-atlas-source)

	add_executable(test-geometry-source
		tests/unit/test_geometry_source.c
		tests/support/geometry_source.c)
	target_link_libraries(test-geometry-source PRIVATE
		mh_model_myo
		mh_world_myw
		mh_geometry_loading
		mh_resource_memory)
	mh_c89(test-geometry-source)
	add_test(NAME geometry-source COMMAND test-geometry-source)

	add_executable(test-model-data tests/unit/test_model_data.c)
	target_link_libraries(test-model-data PRIVATE mh_assets)
	mh_c89(test-model-data)
	add_test(NAME model-data COMMAND test-model-data)

	add_executable(mh-model-data-check tests/assets/model_data.c)
	target_link_libraries(mh-model-data-check PRIVATE
		mh_assets
		mh_file_read_filesystem)
	mh_c89(mh-model-data-check)

	add_executable(test-texture-names tests/unit/test_texture_names.c)
	target_link_libraries(test-texture-names PRIVATE mh_assets)
	mh_c89(test-texture-names)
	add_test(NAME texture-names COMMAND test-texture-names)

	add_executable(test-myo tests/unit/test_myo.c)
	target_link_libraries(test-myo PRIVATE mh_assets)
	mh_c89(test-myo)
	add_test(NAME myo-reader COMMAND test-myo)

	add_executable(test-assets tests/unit/test_assets.c)
	target_link_libraries(test-assets PRIVATE
		mh_file_read_filesystem
		mh_assets
		mh_image_fallback
		mh_image_pdi)
	mh_c89(test-assets)
	add_test(NAME asset-readers COMMAND test-assets)

	add_executable(test-texture tests/unit/test_texture.c)
	target_link_libraries(test-texture PRIVATE
		mh_file_read_filesystem
		mh_assets
		mh_image_fallback
		mh_image_pdi)
	mh_c89(test-texture)
	add_test(NAME track-textures COMMAND test-texture)

	add_executable(test-car tests/unit/test_car.c)
	target_link_libraries(test-car PRIVATE mh_assets)
	mh_c89(test-car)
	add_test(NAME car-config COMMAND test-car)

	add_executable(test-car-paint tests/unit/test_car_paint.c)
	target_link_libraries(test-car-paint PRIVATE mh_assets)
	mh_c89(test-car-paint)
	add_test(NAME car-paint COMMAND test-car-paint)

	add_executable(test-track tests/unit/test_track.c)
	target_link_libraries(test-track PRIVATE mh_game)
	mh_c89(test-track)
	add_test(NAME track-visuals COMMAND test-track)

	add_executable(test-camera-motion tests/unit/test_camera_motion.c)
	target_link_libraries(test-camera-motion PRIVATE mh_assets)
	mh_c89(test-camera-motion)
	add_test(NAME camera-motion COMMAND test-camera-motion)

	add_executable(test-world tests/unit/test_world.c)
	target_link_libraries(test-world PRIVATE mh_assets mh_image_fallback)
	mh_c89(test-world)
	add_test(NAME world-geometry COMMAND test-world)

	add_executable(test-navigation-source
		tests/unit/test_navigation_source.c
		tests/support/navigation_source.c)
	target_link_libraries(test-navigation-source PRIVATE
		mh_navigation_mot_dat
		mh_server_courses
		mh_resource_memory)
	mh_c89(test-navigation-source)
	add_test(NAME navigation-source COMMAND test-navigation-source)

	add_executable(test-animation-source
		tests/unit/test_animation_source.c
		tests/support/animation_source.c)
	target_link_libraries(test-animation-source PRIVATE
		mh_animation
		mh_asset_motion
		mh_resource_memory)
	mh_c89(test-animation-source)
	add_test(NAME animation-source COMMAND test-animation-source)

	add_executable(test-animation tests/unit/test_animation.c)
	target_link_libraries(test-animation PRIVATE mh_animation)
	mh_c89(test-animation)
	add_test(NAME decoded-animation COMMAND test-animation)

	add_executable(test-scene-render tests/unit/test_scene_render.c)
	target_link_libraries(test-scene-render PRIVATE mh_race_scene)
	mh_c89(test-scene-render)
	add_test(NAME scene-render-policy COMMAND test-scene-render)

	add_executable(test-scene tests/unit/test_scene.c)
	target_link_libraries(test-scene PRIVATE mh_assets)
	mh_c89(test-scene)
	add_test(NAME scene-poses COMMAND test-scene)

	add_executable(test-resolver tests/unit/test_resolver.c)
	target_link_libraries(test-resolver PRIVATE mh_resolver)
	mh_c89(test-resolver)
	add_test(NAME resolver-provider COMMAND test-resolver)

	if(WIN32 AND MINGW)
		add_executable(test-server-console-windows
			tests/integration/server_console_windows.c
			src/server/hosts/windows_console.c)
		target_link_libraries(test-server-console-windows PRIVATE
			mh_core)
		target_link_options(test-server-console-windows PRIVATE
			"-Wl,--wrap=__imp_SetConsoleCtrlHandler")
		mh_c89(test-server-console-windows)
		add_test(NAME server-windows-console COMMAND
			test-server-console-windows)
		set_tests_properties(server-windows-console PROPERTIES TIMEOUT
			20)
	endif()

	if(TARGET mh_resolver_windows AND MINGW)
		add_executable(test-resolver-windows
			tests/integration/resolver_windows.c)
		target_link_libraries(test-resolver-windows PRIVATE
			mh_resolver_windows
			ws2_32)
		target_link_options(test-resolver-windows PRIVATE
			"-Wl,--wrap=__imp_getaddrinfo")
		mh_c89(test-resolver-windows)
		add_test(NAME resolver-windows-lifetime COMMAND
			test-resolver-windows)
		set_tests_properties(resolver-windows-lifetime PROPERTIES
			TIMEOUT 30)
	endif()

	if(TARGET mh_resolver_posix AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
		add_executable(test-resolver-posix
			tests/integration/resolver_posix.c)
		target_link_libraries(test-resolver-posix PRIVATE
			mh_resolver_posix
			Threads::Threads)
		target_link_options(test-resolver-posix PRIVATE
			"-Wl,--wrap=getaddrinfo")
		mh_c89(test-resolver-posix)
		add_test(NAME resolver-native-lifetime COMMAND
			test-resolver-posix)
		set_tests_properties(resolver-native-lifetime PROPERTIES
			TIMEOUT 15)
	endif()

	add_executable(test-transport-provider
		tests/unit/test_transport_provider.c)
	target_link_libraries(test-transport-provider PRIVATE
		mh_network_session)
	mh_c89(test-transport-provider)
	add_test(NAME transport-provider COMMAND test-transport-provider)

	add_executable(test-transport-host tests/unit/test_transport_provider.c)
	target_compile_definitions(test-transport-host PRIVATE
		MH_TEST_HOST_NETWORK=1)
	target_link_libraries(test-transport-host PRIVATE
		mh_menus_app
		mh_resource_filesystem)
	mh_c89(test-transport-host)
	add_test(NAME transport-host-selection COMMAND test-transport-host)

	if(MH_ALL_AUDIO_PROFILES)
		add_executable(test-audio-provider
			tests/unit/test_audio_provider.c)
		target_link_libraries(test-audio-provider PRIVATE
			mh_horn_fallback
			mh_material_mat_srf
			mh_environment_esd
			mh_audio_fallback
			mh_animation_lightwave
			mh_audio_playback
			mh_resource_memory)
		mh_c89(test-audio-provider)
		add_test(NAME audio-provider COMMAND test-audio-provider)
	endif()

	if(MH_BUILD_CLIENT)
		if(WIN32)
			add_executable(test-windows-startup
				tests/unit/test_windows_startup.c)
			target_link_libraries(test-windows-startup PRIVATE
				mh_window_sdl_startup)
			mh_c89(test-windows-startup)
			add_test(NAME windows-startup COMMAND
				test-windows-startup)
		endif()

		add_executable(test-platform-input
			tests/integration/platform_input.c)
		target_link_libraries(test-platform-input PRIVATE
			mh_diagnostic_input
			mh_diagnostic_graphics_catalog
			SDL3::SDL3)
		target_include_directories(test-platform-input SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(test-platform-input)
		add_test(NAME platform-input-boundary COMMAND
			test-platform-input)

		add_executable(test-audio-worker
			tests/integration/audio_worker.c)
		target_link_libraries(test-audio-worker PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(test-audio-worker SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(test-audio-worker)
		add_test(NAME effects-worker COMMAND test-audio-worker)
		set_tests_properties(effects-worker PROPERTIES ENVIRONMENT
			"SDL_AUDIO_DRIVER=dummy" TIMEOUT 15)

		add_executable(test-halo-visibility
			tests/integration/halo_visibility.c)
		target_link_libraries(test-halo-visibility PRIVATE
			mh_diagnostic_runtime
			mh_image_fallback)
		mh_c89(test-halo-visibility)
		add_test(NAME attached-halo-visibility COMMAND
			test-halo-visibility)

		if(UNIX)
			add_executable(test-drive-scores
				tests/unit/test_drive_scores.c)
			target_link_libraries(test-drive-scores PRIVATE
				mh_diagnostic_runtime
				mh_highscore_files)
			mh_c89(test-drive-scores)
			add_test(NAME drive-high-score-awards COMMAND
				test-drive-scores)
		endif()

		add_executable(mh-music-assets tests/assets/music_assets.c)
		target_link_libraries(mh-music-assets PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(mh-music-assets SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-music-assets)

		add_executable(mh-audio-clock tests/integration/audio_clock.c)
		target_link_libraries(mh-audio-clock PRIVATE
			mh_horn_fallback
			mh_catalog_fallback
			mh_diagnostic_runtime)
		mh_c89(mh-audio-clock)

		add_executable(test-graphics-catalog
			tests/integration/graphics_catalog.c)
		target_link_libraries(test-graphics-catalog PRIVATE
			mh_diagnostic_runtime
			SDL3::SDL3)
		target_include_directories(test-graphics-catalog SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(test-graphics-catalog)
		add_test(NAME native-graphics-catalog COMMAND
			test-graphics-catalog)
		set_tests_properties(native-graphics-catalog PROPERTIES
			ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")

		add_executable(mh-patched-demo-assets
			tests/assets/patched_demo_assets.c)
		target_link_libraries(mh-patched-demo-assets PRIVATE
			mh_diagnostic_runtime
			mh_file_read_filesystem
			mh_demo_setup
			mh_user_data_memory
			SDL3::SDL3)
		target_include_directories(mh-patched-demo-assets SYSTEM PRIVATE
			"$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
		mh_c89(mh-patched-demo-assets)

		if(UNIX)
			add_executable(test-network-recording-path
				tests/unit/test_network_recording_path.c)
			target_link_libraries(test-network-recording-path PRIVATE
				mh_diagnostic_runtime)
			mh_c89(test-network-recording-path)
			add_test(NAME network-recording-path COMMAND
				test-network-recording-path)

			add_executable(mh-demo-recording-assets
				tests/assets/demo_recording_assets.c
				tests/support/recording_failure.c)
			target_link_libraries(mh-demo-recording-assets PRIVATE
				mh_diagnostic_runtime
				mh_file_read_filesystem
				mh_demo_setup
				mh_playback_hud
				mh_user_data_memory)
			mh_c89(mh-demo-recording-assets)

			add_executable(mh-ghost-recording-assets
				tests/assets/ghost_recording_assets.c
				tests/support/recording_failure.c)
			target_link_libraries(mh-ghost-recording-assets PRIVATE
				mh_diagnostic_runtime
				mh_file_read_filesystem
				mh_demo_setup
				mh_playback_hud)
			mh_c89(mh-ghost-recording-assets)

			add_executable(mh-network-recording-assets
				tests/assets/network_recording_assets.c)
			target_link_libraries(mh-network-recording-assets PRIVATE
				mh_diagnostic_runtime
				mh_file_read_filesystem
				mh_network_setup
				mh_demo_setup
				mh_demo_recording)
			mh_c89(mh-network-recording-assets)
		endif()

		add_executable(mh-scenery-drive-check
			tests/integration/scenery_drive.c)
		target_link_libraries(mh-scenery-drive-check PRIVATE
			mh_diagnostic_runtime)
		mh_c89(mh-scenery-drive-check)

		add_executable(mh-world-secrets-assets
			tests/assets/world_secrets_assets.c)
		target_link_libraries(mh-world-secrets-assets PRIVATE
			mh_diagnostic_runtime)
		mh_c89(mh-world-secrets-assets)
		file(MAKE_DIRECTORY
			"${CMAKE_CURRENT_BINARY_DIR}/path-fixture/Cars")
		file(WRITE
			"${CMAKE_CURRENT_BINARY_DIR}/path-fixture/Cars/test.myo"
			"fixture")

		add_executable(test-asset-path tests/unit/test_asset_path.c)
		target_link_libraries(test-asset-path PRIVATE
			mh_diagnostic_runtime)
		mh_c89(test-asset-path)
		add_test(NAME asset-path COMMAND test-asset-path
			"${CMAKE_CURRENT_BINARY_DIR}/path-fixture")
	endif()
endif()

# Compile-only probe: no window, GPU or SDL runtime is needed.
set(MH_SDL3_INCLUDE_DIR "" CACHE PATH
	"Directory containing SDL3/ for the optional C89 header probe")

if(MH_SDL3_INCLUDE_DIR)
	add_library(mh-sdl89-check OBJECT tests/support/sdl89.c)
	target_include_directories(mh-sdl89-check SYSTEM PRIVATE
		"${MH_SDL3_INCLUDE_DIR}")
	mh_c89(mh-sdl89-check)
endif()

if(BUILD_TESTING AND UNIX)
	add_executable(test-picture-store tests/unit/test_picture_store.c)
	target_link_libraries(test-picture-store PRIVATE
		mh_file_read_filesystem
		mh_picture_files)
	mh_c89(test-picture-store)
	add_test(NAME screenshot-files COMMAND test-picture-store)

	add_executable(test-control-store tests/unit/test_control_store.c)
	target_link_libraries(test-control-store PRIVATE
		mh_file_read_filesystem
		mh_control_files
		mh_assets)
	mh_c89(test-control-store)
	add_test(NAME control-profile-store COMMAND test-control-store)

	add_executable(test-address-store tests/unit/test_address_store.c)
	target_link_libraries(test-address-store PRIVATE mh_address_files)
	mh_c89(test-address-store)
	add_test(NAME tcp-address-store COMMAND test-address-store)

	add_executable(test-track-flag-files tests/unit/test_track_flag_files.c)
	target_link_libraries(test-track-flag-files PRIVATE mh_track_flag_files)
	mh_c89(test-track-flag-files)
	add_test(NAME track-marker-files COMMAND test-track-flag-files)

	add_executable(test-track-flag-save tests/unit/test_track_flag_save.c)
	target_link_libraries(test-track-flag-save PRIVATE mh_track_flag_files)
	mh_c89(test-track-flag-save)
	add_test(NAME track-marker-save COMMAND test-track-flag-save)

	add_executable(test-file-replace tests/unit/test_file_replace.c)
	target_link_libraries(test-file-replace PRIVATE mh_file_replace)
	mh_c89(test-file-replace)
	add_test(NAME file-replace-failures COMMAND test-file-replace)

	add_executable(test-settings-files tests/unit/test_settings_files.c)
	target_link_libraries(test-settings-files PRIVATE mh_settings_files)
	mh_c89(test-settings-files)
	add_test(NAME settings-files COMMAND test-settings-files)

	add_executable(test-highscore-files tests/unit/test_highscore_files.c)
	target_link_libraries(test-highscore-files PRIVATE mh_highscore_files)
	mh_c89(test-highscore-files)
	add_test(NAME high-score-files COMMAND test-highscore-files)

	add_executable(test-record-files tests/unit/test_record_files.c)
	target_link_libraries(test-record-files PRIVATE mh_record_files)
	mh_c89(test-record-files)
	add_test(NAME record-files COMMAND test-record-files)

	add_executable(test-league-files tests/unit/test_league_files.c)
	target_link_libraries(test-league-files PRIVATE mh_league_files)
	mh_c89(test-league-files)
	add_test(NAME league-files COMMAND test-league-files)

	add_executable(test-league-store tests/unit/test_league_store.c)
	target_link_libraries(test-league-store PRIVATE
		mh_file_read_filesystem
		mh_league_files)
	mh_c89(test-league-store)
	add_test(NAME league-store COMMAND test-league-store)

	add_executable(test-leagues tests/unit/test_leagues.c)
	target_link_libraries(test-leagues PRIVATE
		mh_diagnostic_leagues
		mh_league_files
		mh_ui)
	mh_c89(test-leagues)
	add_test(NAME league-collection COMMAND test-leagues)
endif()

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
	if(NOT EMSCRIPTEN)
		add_test(NAME native-executable-layout
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/check.py"
				release_layout --build "${CMAKE_BINARY_DIR}")
	endif()

	add_test(NAME source-constants
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_constants.py")
	add_test(NAME portable-backend-boundaries
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_backend_boundaries.py")
	add_test(NAME development-build-paths
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_build_paths.py")
	add_test(NAME development-tool-support
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_tool_support.py")
	add_test(NAME development-check-runner
		COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_check_runner.py")
endif()

if(BUILD_TESTING)
	add_executable(test-powervr-order tests/unit/test_powervr_order.c)
	target_link_libraries(test-powervr-order PRIVATE mh_render_common)
	mh_c89(test-powervr-order)
	add_test(NAME powervr-region-order COMMAND test-powervr-order)
endif()

if(BUILD_TESTING AND TARGET mh_host_common_services)
	add_executable(test-host-services tests/unit/test_host_services.c)
	target_link_libraries(test-host-services PRIVATE
		mh_host_common_services)
	mh_c89(test-host-services)
	add_test(NAME shared-host-services COMMAND test-host-services)
endif()

if(BUILD_TESTING AND WIN32)
	add_executable(test-windows-text tests/unit/test_windows_text.c)
	target_link_libraries(test-windows-text PRIVATE
		mh_windows_text
		mh_filesystem_io)
	mh_c89(test-windows-text)
	add_test(NAME windows-text-conversion COMMAND test-windows-text)
endif()

if(BUILD_TESTING)
	add_executable(test-config-line tests/unit/test_config_line.c)
	target_link_libraries(test-config-line PRIVATE mh_assets)
	mh_c89(test-config-line)
	add_test(NAME config-line-policy COMMAND test-config-line)

	add_executable(test-asset-reference tests/unit/test_asset_reference.c)
	target_link_libraries(test-asset-reference PRIVATE mh_resource_source)
	mh_c89(test-asset-reference)
	add_test(NAME asset-reference-extension COMMAND test-asset-reference)
endif()

if(BUILD_TESTING)
	add_executable(test-material-classify
		tests/unit/test_material_classify.c)
	target_link_libraries(test-material-classify PRIVATE mh_render_common)
	mh_c89(test-material-classify)
	add_test(NAME material-classification COMMAND test-material-classify)

	add_executable(test-session-race-options
		tests/unit/test_session_race_options.c)
	target_link_libraries(test-session-race-options PRIVATE
		mh_session_race_options)
	mh_c89(test-session-race-options)
	add_test(NAME session-race-options COMMAND test-session-race-options)
endif()
